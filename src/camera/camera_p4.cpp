// ESP32-P4 MIPI-CSI camera (Waveshare ESP32-P4-ETH / -Module-DEV-KIT connector; OV5647 or
// SC2336 modules) through the esp_video component bundled with the Arduino core: a V4L2 device
// (/dev/video0) fed by the CSI controller and ISP. The hardware JPEG *video device* is not
// enabled in the Arduino sdkconfig, so the frame is taken as RGB565 at the sensor's native
// size, scaled down by an integer factor to <= 640 wide and encoded with the P4's hardware
// JPEG encoder (esp_driver_jpeg). Config: {"type":"p4_csi","sensor":"auto","sda":7,"scl":8}.
// The sensor's SCCB bus shares Wire (I2C port 0) when hw["i2c"] uses the same pins, else it
// gets its own bus on I2C port 1.
#include "camera.h"

#if CONFIG_IDF_TARGET_ESP32P4 && __has_include("esp_video_init.h")
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include "../hwconfig.h"
#include "driver/i2c_master.h"
#include "driver/jpeg_encode.h"
#include "esp_video_device.h"
#include "esp_video_init.h"

namespace {

static const int BUFFERS = 2;
static const int WARMUP = 8;  // frames taken per capture so exposure settles; the last is kept
static const int QUALITY = 80;

class P4Csi : public CameraBackend {
  int fd = -1;
  uint32_t w = 0, h = 0;    // sensor frame
  uint32_t ow = 0, oh = 0;  // stored JPEG
  int scale = 1;
  void* buf[BUFFERS] = {};
  size_t bufLen[BUFFERS] = {};
  jpeg_encoder_handle_t enc = nullptr;
  uint8_t* in = nullptr;
  uint8_t* out = nullptr;
  size_t inSize = 0, outSize = 0;
  String sensor, wanted;

  bool fail(const String& why) {
    error = why;
    return false;
  }

 public:
  bool begin(JsonVariantConst cfg, CameraSize size) override {
    int sda = hwPin(cfg, "sda", 7), scl = hwPin(cfg, "scl", 8);
    wanted = cfg["sensor"] | "auto";

    esp_video_init_csi_config_t csi = {};
    JsonVariantConst i2c = hw["i2c"];
    if (hwPin(i2c, "sda") == sda && hwPin(i2c, "scl") == scl) {
      // Wire already runs I2C port 0 on these pins: use its bus
      i2c_master_bus_handle_t bus = nullptr;
      if (i2c_master_get_bus_handle(0, &bus) != ESP_OK || !bus) return fail("Wire bus is not up");
      csi.sccb_config.init_sccb = false;
      csi.sccb_config.i2c_handle = bus;
    } else {
      csi.sccb_config.init_sccb = true;
      csi.sccb_config.i2c_config.port = 1;
      csi.sccb_config.i2c_config.sda_pin = (gpio_num_t)sda;
      csi.sccb_config.i2c_config.scl_pin = (gpio_num_t)scl;
    }
    csi.sccb_config.freq = 100000;
    csi.reset_pin = GPIO_NUM_NC;
    csi.pwdn_pin = GPIO_NUM_NC;
    csi.dont_init_ldo = false;  // esp_video powers the MIPI PHY (LDO channel 3, 2.5 V)
    esp_video_init_config_t vc = {};
    vc.csi = &csi;
    esp_err_t err = esp_video_init_with_flags(&vc, ESP_VIDEO_INIT_FLAGS_MIPI_CSI | ESP_VIDEO_INIT_FLAGS_ISP);
    if (err != ESP_OK) return fail(String("no sensor on the CSI connector (") + esp_err_to_name(err) + ")");

    fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (fd < 0) return fail("cannot open /dev/video0");
    v4l2_capability cap = {};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) sensor = (const char*)cap.card;

    // the CSI device only streams the sensor's own size; pick RGB565 for the encoder
    v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) return fail("VIDIOC_G_FMT failed");
    w = fmt.fmt.pix.width;
    h = fmt.fmt.pix.height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) return fail("RGB565 output not supported");

    v4l2_requestbuffers req = {};
    req.count = BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) return fail("VIDIOC_REQBUFS failed");
    for (int i = 0; i < BUFFERS; i++) {
      v4l2_buffer b = {};
      b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      b.memory = V4L2_MEMORY_MMAP;
      b.index = i;
      if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) return fail("VIDIOC_QUERYBUF failed");
      buf[i] = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
      bufLen[i] = b.length;
      if (buf[i] == MAP_FAILED) return fail("mmap failed");
    }

    // integer downscale so the photo is at most 640 (or 320) wide; JPEG wants 16x8 blocks
    uint32_t maxW = size == CAM_VGA ? 640 : 320;
    scale = 1;
    while (w / scale > maxW) scale++;
    ow = (w / scale) & ~15u;
    oh = (h / scale) & ~7u;

    jpeg_encode_engine_cfg_t ec = {};
    ec.intr_priority = 0;
    ec.timeout_ms = 1000;
    if (jpeg_new_encoder_engine(&ec, &enc) != ESP_OK) return fail("JPEG encoder init failed");
    jpeg_encode_memory_alloc_cfg_t ic = {JPEG_ENC_ALLOC_INPUT_BUFFER};
    jpeg_encode_memory_alloc_cfg_t oc = {JPEG_ENC_ALLOC_OUTPUT_BUFFER};
    in = (uint8_t*)jpeg_alloc_encoder_mem(ow * oh * 2, &ic, &inSize);
    out = (uint8_t*)jpeg_alloc_encoder_mem(ow * oh, &oc, &outSize);  // 1 byte/pixel is plenty
    if (!in || !out) return fail("out of memory for the JPEG encoder");
    return true;
  }

  size_t capture(Print& to) override {
    if (fd < 0) return 0;
    for (int i = 0; i < BUFFERS; i++) {
      v4l2_buffer b = {};
      b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      b.memory = V4L2_MEMORY_MMAP;
      b.index = i;
      if (ioctl(fd, VIDIOC_QBUF, &b) < 0) { error = "VIDIOC_QBUF failed"; return 0; }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) { error = "VIDIOC_STREAMON failed"; return 0; }
    v4l2_buffer b = {};
    bool got = false;
    for (int n = 0; n < WARMUP; n++) {
      b = {};
      b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      b.memory = V4L2_MEMORY_MMAP;
      if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) break;
      if (n == WARMUP - 1) { got = true; break; }
      ioctl(fd, VIDIOC_QBUF, &b);
    }
    // nearest-neighbour downscale into the encoder's buffer, then stop the stream
    if (got) {
      const uint16_t* src = (const uint16_t*)buf[b.index];
      uint16_t* dst = (uint16_t*)in;
      for (uint32_t y = 0; y < oh; y++) {
        const uint16_t* row = src + (size_t)y * scale * w;
        for (uint32_t x = 0; x < ow; x++) dst[y * ow + x] = row[x * scale];
      }
    }
    ioctl(fd, VIDIOC_STREAMOFF, &type);  // also hands every buffer back
    if (!got) { error = "no frame from the sensor"; return 0; }

    jpeg_encode_cfg_t jc = {};
    jc.width = ow;
    jc.height = oh;
    jc.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
    jc.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
    jc.image_quality = QUALITY;
    jc.pixel_reverse = false;
    uint32_t len = 0;
    esp_err_t err = jpeg_encoder_process(enc, &jc, in, ow * oh * 2, out, outSize, &len);
    if (err != ESP_OK || !len) { error = String("JPEG encode: ") + esp_err_to_name(err); return 0; }
    if (to.write(out, len) != len) { error = "write failed"; return 0; }
    error = "";
    return len;
  }

  String name() override {
    String n = "MIPI-CSI " + (sensor.length() ? sensor : String("camera"));
    if (wanted != "auto" && sensor.length() && !sensor.equalsIgnoreCase(wanted))
      n += " (config says " + wanted + ")";
    return n;
  }
};

}  // namespace

CameraBackend* makeP4Csi() { return new P4Csi(); }

#else  // not an ESP32-P4 build (or esp_video missing from the core)

CameraBackend* makeP4Csi() { return nullptr; }

#endif
