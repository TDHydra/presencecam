// Camera front end: picks the backend for hw["camera"] and keeps the photos in storageFs().
//
// /photos/NNNN.jpg holds the JPEGs, /photos/index.txt one line per photo, oldest first:
//   name|unix_time|reason|bytes
// The oldest photos go when there are more than `keep` or when the partition is full.
#include "camera.h"
#include "../debuglog.h"


#include <vector>

#include "../hwconfig.h"
#include "../storage.h"

static const char* DIR = "/photos";
static const char* INDEX = "/photos/index.txt";

// What fits: the 4 MB boards (min_spiffs.csv) have a 128 KB partition shared with the event
// log, the 16 MB ESP32-S3 / ESP32-P4 builds (default_16MB.csv) 3.4 MB.
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
static const int DEFAULT_KEEP = 20;
static const CameraSize DEFAULT_SIZE = CAM_VGA;
#else
static const int DEFAULT_KEEP = 3;
static const CameraSize DEFAULT_SIZE = CAM_QVGA;
#endif
static const size_t FS_RESERVE = 16 * 1024;  // always left free for the event log

static CameraBackend* cam = nullptr;
static bool camOk = false;
static int keep = DEFAULT_KEEP;
static CameraSize size = DEFAULT_SIZE;
static std::vector<PhotoInfo> photos;  // newest first
static bool indexLoaded = false;

static const char* sizeName() { return size == CAM_VGA ? "640x480" : "320x240"; }

// Photos go to the microSD card when there is one, else to flash (see storage.h).
static bool fsReady() { return storageTotal() > 0; }

static void loadIndex() {
  if (indexLoaded || !fsReady()) return;
  indexLoaded = true;
  photos.clear();
  File f = storageFs().open(INDEX, "r");
  if (!f) return;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    int a = l.indexOf('|'), b = l.indexOf('|', a + 1), c = l.lastIndexOf('|');
    if (a < 0 || b < 0 || c <= b) continue;
    PhotoInfo p;
    p.path = String(DIR) + "/" + l.substring(0, a);
    p.time = strtoul(l.substring(a + 1, b).c_str(), nullptr, 10);
    p.reason = l.substring(b + 1, c);
    p.bytes = strtoul(l.substring(c + 1).c_str(), nullptr, 10);
    if (storageFs().exists(p.path)) photos.insert(photos.begin(), p);
  }
  f.close();
}

static void saveIndex() {
  File f = storageFs().open(INDEX, "w");
  if (!f) return;
  for (int i = (int)photos.size() - 1; i >= 0; i--) {
    const PhotoInfo& p = photos[i];
    String reason = p.reason;
    reason.replace("|", "/");
    reason.replace("\n", " ");
    String name = p.path.substring(strlen(DIR) + 1);
    f.println(name + "|" + String(p.time) + "|" + reason + "|" + String(p.bytes));
  }
  f.close();
}

static void dropOldest() {
  if (photos.empty()) return;
  storageFs().remove(photos.back().path);
  photos.pop_back();
}

// Makes room for one more photo of about `bytes` (0 = unknown, assume 64 KB).
static void makeRoom(size_t bytes) {
  if (!bytes) bytes = 64 * 1024;
  while ((int)photos.size() >= keep) dropOldest();
  while (!photos.empty() && storageFree() < bytes + FS_RESERVE) dropOldest();
}

static String nextPath() {
  uint32_t n = 0;
  if (!photos.empty()) n = strtoul(photos.front().path.substring(strlen(DIR) + 1).c_str(), nullptr, 10);
  n = n % 9999 + 1;
  char name[16];
  snprintf(name, sizeof name, "%s/%04u.jpg", DIR, (unsigned)n);
  return String(name);
}

void cameraBegin() {
  JsonVariantConst cfg = hw["camera"];
  if (cfg.isNull() || hwSafeMode) return;
  String type = cfg["type"] | "";
  if (type == "arducam_mega") cam = makeArducamMega();
  else if (type == "esp32cam") cam = makeEsp32Cam();
  else if (type == "p4_csi") cam = makeP4Csi();
  if (!cam) {
    Serial.println("camera: type \"" + type + "\" is not available in this build");
    return;
  }
  keep = cfg["keep"] | DEFAULT_KEEP;
  if (keep < 1) keep = 1;
  String s = cfg["size"] | "";
  if (s == "qvga") size = CAM_QVGA;
  else if (s == "vga") size = CAM_VGA;
  camOk = cam->begin(cfg, size);
  if (camOk) Serial.println("camera: " + cam->name() + ", " + sizeName() + ", keeping " + String(keep));
  else Serial.println("camera: " + cam->name() + " not found: " + cam->error);
}

bool cameraPresent() { return cam && camOk; }

String cameraStatus() {
  if (!cam) return "none";
  if (!camOk) return cam->error.length() ? "not found (" + cam->error + ")" : "not found";
  loadIndex();
  return cam->name() + ", " + sizeName() + ", " + String(photos.size()) + " photo" +
         (photos.size() == 1 ? "" : "s");
}

String cameraCapture(const String& reason) {
  if (!cameraPresent()) return "";
  loadIndex();
  if (!fsReady()) { cam->error = "no storage mounted"; return ""; }
  if (!storageFs().exists(DIR)) storageFs().mkdir(DIR);
  makeRoom(0);
  String path = nextPath();
  File f = storageFs().open(path, "w");
  if (!f) { cam->error = "cannot create " + path; return ""; }
  size_t bytes = cam->capture(f);
  bool writeErr = f.getWriteError();
  f.close();
  if (bytes == 0 || writeErr) {
    storageFs().remove(path);
    if (cam->error.isEmpty()) cam->error = "capture failed";
    return "";
  }
  PhotoInfo p{path, (uint32_t)time(nullptr), reason, bytes};
  if (p.time < 1600000000) p.time = 0;  // clock not set
  photos.insert(photos.begin(), p);
  saveIndex();
  return path;
}

int cameraPhotoCount() {
  loadIndex();
  return photos.size();
}

bool cameraPhotoAt(int i, PhotoInfo& out) {
  loadIndex();
  if (i < 0 || i >= (int)photos.size()) return false;
  out = photos[i];
  return true;
}

bool cameraSendPhoto(const String& path, Print& to) {
  if (!path.startsWith(String(DIR) + "/") || path.indexOf("..") >= 0 || !fsReady()) return false;
  File f = storageFs().open(path, "r");
  if (!f) return false;
  uint8_t buf[1024];
  while (f.available()) {
    size_t n = f.read(buf, sizeof buf);
    if (to.write(buf, n) != n) { f.close(); return false; }
  }
  f.close();
  return true;
}
