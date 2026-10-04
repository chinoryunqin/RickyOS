// Desktop-only SDL bridge. Loaded into the native simulator, never into firmware.
// The pinned simulator remains untouched. All SDL/input work stays on its main thread.
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
constexpr size_t kPixels = 1216 * 684;
// Host-only, bounded snapshot storage: retain the uploaded texture until Present,
// then rotate into native logical pixels. Neither buffer exists on the ESP32.
std::array<unsigned char, kPixels> physical{}, logical{};
SDL_Texture* screenTexture = nullptr;
int physicalWidth = 0, physicalHeight = 0;
int angle = 0;
bool dirty = false;
unsigned sequence = 0;
std::array<Uint8, SDL_NUM_SCANCODES> webKeys{}, combinedKeys{};
struct TimedEvent { Uint32 at; SDL_Event event; };
std::array<TimedEvent, 128> events{};
size_t eventCount = 0;
Uint32 lastEnd = 0;
int inputFd = -1;
bool inputInitialized = false;

const char* bridgeDir() { return std::getenv("RICKYOS_BRIDGE_DIR"); }
bool pathFor(char* out, size_t size, const char* name) {
  const char* dir = bridgeDir();
  return dir && std::snprintf(out, size, "%s/%s", dir, name) < static_cast<int>(size);
}

void openInput() {
  if (inputInitialized) return;
  inputInitialized = true;
  if (!bridgeDir()) return;
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (!pathFor(address.sun_path, sizeof(address.sun_path), "input.sock")) return;
  inputFd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (inputFd < 0) return;
  unlink(address.sun_path);  // Owned temporary bridge socket only, including exec/wake.
  if (bind(inputFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close(inputFd); inputFd = -1; return;
  }
  fcntl(inputFd, F_SETFL, O_NONBLOCK);
  fcntl(inputFd, F_SETFD, FD_CLOEXEC);
}

SDL_Scancode namedKey(const char* name) {
  if (!strcmp(name, "BACK")) return SDL_SCANCODE_ESCAPE;
  if (!strcmp(name, "ENTER")) return SDL_SCANCODE_RETURN;
  if (!strcmp(name, "UP")) return SDL_SCANCODE_UP;
  if (!strcmp(name, "DOWN")) return SDL_SCANCODE_DOWN;
  if (!strcmp(name, "LEFT")) return SDL_SCANCODE_LEFT;
  if (!strcmp(name, "RIGHT")) return SDL_SCANCODE_RIGHT;
  if (!strcmp(name, "POWER")) return SDL_SCANCODE_P;
  if (!strcmp(name, "SLEEP")) return SDL_SCANCODE_S;
  return SDL_SCANCODE_UNKNOWN;
}

void addTouch(Uint32 at, Uint32 type, float x, float y) {
  const bool portrait = angle == 90 || angle == 270;
  const int w = portrait ? physicalHeight : physicalWidth;
  const int h = portrait ? physicalWidth : physicalHeight;
  SDL_Event event{};
  event.type = type;
  if (type == SDL_MOUSEMOTION) {
    event.motion.x = static_cast<int>(x * (w - 1));
    event.motion.y = static_cast<int>(y * (h - 1));
    event.motion.state = SDL_BUTTON_LMASK;
  } else {
    event.button.button = SDL_BUTTON_LEFT;
    event.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.button.x = static_cast<int>(x * (w - 1));
    event.button.y = static_cast<int>(y * (h - 1));
  }
  events[eventCount++] = {at, event};
}

void receiveInputs() {
  openInput();
  if (inputFd < 0) return;
  char command[256];
  for (int n = 0; n < 16; ++n) {
    const ssize_t bytes = recv(inputFd, command, sizeof(command) - 1, 0);
    if (bytes <= 0) break;
    command[bytes] = '\0';
    const Uint32 now = SDL_GetTicks();
    const Uint32 start = static_cast<Sint32>(lastEnd - now) > 0 ? lastEnd + 25 : now;
    unsigned duration = 80;
    char key[16]{};
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (std::sscanf(command, "K %15s %u", key, &duration) == 2) {
      const auto scancode = namedKey(key);
      if (scancode == SDL_SCANCODE_UNKNOWN || duration < 40 || duration > 1500 || eventCount + 2 > events.size()) continue;
      SDL_Event down{};
      down.type = SDL_KEYDOWN; down.key.state = SDL_PRESSED;
      down.key.keysym.scancode = scancode;
      SDL_Event up = down; up.type = SDL_KEYUP; up.key.state = SDL_RELEASED;
      events[eventCount++] = {start, down};
      events[eventCount++] = {start + duration, up};
      lastEnd = start + duration;
    } else if (std::sscanf(command, "T %f %f %f %f %u", &x1, &y1, &x2, &y2, &duration) == 5) {
      if (eventCount + 10 > events.size() || duration < 40 || duration > 650 || physicalWidth == 0) continue;
      if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2) ||
          std::min({x1, y1, x2, y2}) < 0 || std::max({x1, y1, x2, y2}) > 1) continue;
      addTouch(start, SDL_MOUSEBUTTONDOWN, x1, y1);
      for (unsigned step = 1; step <= 8; ++step) {
        const float t = step / 8.0f;
        addTouch(start + duration * step / 9, SDL_MOUSEMOTION, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t);
      }
      addTouch(start + duration, SDL_MOUSEBUTTONUP, x2, y2);
      lastEnd = start + duration;
    }
  }
}

void exportFrame() {
  if (!dirty || !bridgeDir()) return;
  dirty = false;
  const bool portrait = angle == 90 || angle == 270;
  const unsigned width = portrait ? physicalHeight : physicalWidth;
  const unsigned height = portrait ? physicalWidth : physicalHeight;
  if (width == 0 || height == 0 || width * height > kPixels) return;
  for (unsigned y = 0; y < height; ++y) {
    for (unsigned x = 0; x < width; ++x) {
      unsigned px = x, py = y;
      if (angle == 90) { px = y; py = physicalHeight - 1 - x; }
      else if (angle == 270) { px = physicalWidth - 1 - y; py = x; }
      else if (angle == 180) { px = physicalWidth - 1 - x; py = physicalHeight - 1 - y; }
      logical[y * width + x] = physical[py * physicalWidth + px];
    }
  }
  char temporary[1024], finalPath[1024];
  if (!pathFor(temporary, sizeof(temporary), "frame.tmp") || !pathFor(finalPath, sizeof(finalPath), "frame.gray")) return;
  FILE* file = std::fopen(temporary, "wb");
  if (!file) return;
  // Fixed little-endian protocol; supported host is Apple Silicon/Intel macOS.
  const unsigned header[4] = {0x31424b52, width, height, ++sequence}; // RKB1
  bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header);
  ok = ok && std::fwrite(logical.data(), 1, width * height, file) == width * height;
  ok = std::fclose(file) == 0 && ok;
  if (ok) std::rename(temporary, finalPath);
}
} // namespace

int rickyUpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch) {
  const int result = SDL_UpdateTexture(texture, rect, pixels, pitch);
  Uint32 format = 0; int width = 0, height = 0;
  if (result == 0 && !rect && pixels && SDL_QueryTexture(texture, &format, nullptr, &width, &height) == 0 &&
      format == SDL_PIXELFORMAT_ARGB8888 && width > 0 && height > 0 &&
      static_cast<size_t>(width) * height <= kPixels && pitch >= width * 4) {
    screenTexture = texture; physicalWidth = width; physicalHeight = height;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        Uint32 value;
        std::memcpy(&value, static_cast<const char*>(pixels) + y * pitch + x * 4, sizeof(value));
        physical[y * width + x] = static_cast<unsigned char>(value >> 16);
      }
    }
    dirty = true;
  }
  return result;
}

int rickyCopyEx(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* src, const SDL_Rect* dst,
                double degrees, const SDL_Point* center, SDL_RendererFlip flip) {
  if (texture == screenTexture) angle = (static_cast<int>(std::lround(degrees)) % 360 + 360) % 360;
  return SDL_RenderCopyEx(renderer, texture, src, dst, degrees, center, flip);
}
int rickyCopy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* src, const SDL_Rect* dst) {
  if (texture == screenTexture) angle = 0;
  return SDL_RenderCopy(renderer, texture, src, dst);
}
void rickyPresent(SDL_Renderer* renderer) { exportFrame(); SDL_RenderPresent(renderer); }

int rickyPoll(SDL_Event* event) {
  receiveInputs();
  if (event && eventCount && static_cast<Sint32>(SDL_GetTicks() - events[0].at) >= 0) {
    *event = events[0].event;
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP)
      webKeys[event->key.keysym.scancode] = event->type == SDL_KEYDOWN;
    std::move(events.begin() + 1, events.begin() + eventCount, events.begin());
    --eventCount;
    return 1;
  }
  return SDL_PollEvent(event);
}
const Uint8* rickyKeyboard(int* count) {
  int length = 0;
  const Uint8* original = SDL_GetKeyboardState(&length);
  const int copied = std::min(length, static_cast<int>(combinedKeys.size()));
  for (int i = 0; i < copied; ++i) combinedKeys[i] = original[i] || webKeys[i];
  if (count) *count = copied;
  return combinedKeys.data();
}

// dyld interposes executable references, not calls from this replacement image.
// This adapter is macOS-only; it does not alter/rewrite the simulator executable.
#define RICKY_INTERPOSE(replacement, original) \
  __attribute__((used)) static struct { const void* replace; const void* replacee; } \
  interpose_##original __attribute__((section("__DATA,__interpose"))) = \
      {reinterpret_cast<const void*>(&replacement), reinterpret_cast<const void*>(&original)};
RICKY_INTERPOSE(rickyUpdateTexture, SDL_UpdateTexture)
RICKY_INTERPOSE(rickyCopyEx, SDL_RenderCopyEx)
RICKY_INTERPOSE(rickyCopy, SDL_RenderCopy)
RICKY_INTERPOSE(rickyPresent, SDL_RenderPresent)
RICKY_INTERPOSE(rickyPoll, SDL_PollEvent)
RICKY_INTERPOSE(rickyKeyboard, SDL_GetKeyboardState)
