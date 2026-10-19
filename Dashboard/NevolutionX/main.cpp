#include "i18n.hpp"

#include <SDL.h>

#include <SDL_image.h>

#include <algorithm>

#include <cstdio>

#include <memory>

#include <mutex>

#include <vector>

#include "config.hpp"

#include "dashboardPaths.hpp"

#include "xisoKernel.hpp"

#include "dvdCopy.hpp"

#include "font.hpp"

#include "xbeLauncher.hpp"

#include "ftpServer.hpp"

#include "infoLog.hpp"

#include "langMenu.hpp"

#include "menu.hpp"

#include "networkManager.hpp"

#include "renderer.hpp"

#include "sntpClient.hpp"

#include "subAppRouter.hpp"

#include "subsystems.hpp"

#include "timeMenu.hpp"

#include "timing.hpp"

#include "xbeArtwork.hpp"

#include "xbeLauncher.hpp"

#ifdef NXDK

#include <hal/video.h>

#include <hal/xbox.h>

extern "C" void usbh_core_deinit(void);

#include <windows.h>

#include "eeprom.hpp"

#endif

#ifdef NXDK

#define SEPARATOR "\\"

#define HOME "A:" SEPARATOR

#else

#define SEPARATOR "/"

#define HOME "." SEPARATOR

#endif

#ifdef NXDK

static std::string formatFreeSpace(char drive) {

  char path[] = { drive, ':', '\\', '\0' };

  ULARGE_INTEGER freeBytes = {};

  if (!GetDiskFreeSpaceExA(path, &freeBytes, nullptr, nullptr)) {

    return std::string(1, drive) + ": not mounted";

  }

  const unsigned long long gib = 1024ULL * 1024ULL * 1024ULL;

  const unsigned long long tenths = (freeBytes.QuadPart * 10ULL + gib / 2ULL) / gib;

  char label[64] = {};

  snprintf(label, sizeof(label), i18n::c("%c free %llu.%lluG"), drive, tenths / 10ULL,

           tenths % 10ULL);

  return label;

}

static bool dvdGameInfo(std::string& title) {

  // Launch DVD uses this kernel device path. Open it directly too, avoiding

  // DOS drive mappings and stdio path translation for the presence check.

  ANSI_STRING path;

  RtlInitAnsiString(&path, "\\Device\\CdRom0\\default.xbe");

  OBJECT_ATTRIBUTES attributes;

  InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

  IO_STATUS_BLOCK io = {};

  HANDLE file = CreateFileA("D:\\default.xbe", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,

                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  NTSTATUS status = file != INVALID_HANDLE_VALUE ? 0 : NtOpenFile(&file, FILE_GENERIC_READ, &attributes, &io,

                               FILE_SHARE_READ | FILE_SHARE_WRITE,

                               FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);

  if (!NT_SUCCESS(status)) return false;

  title.clear();

  unsigned char header[0x11C] = {};

  DWORD count = 0;

  const auto readLe32 = [](const unsigned char* bytes) -> uint32_t {

    return static_cast<uint32_t>(bytes[0]) |

           (static_cast<uint32_t>(bytes[1]) << 8) |

           (static_cast<uint32_t>(bytes[2]) << 16) |

           (static_cast<uint32_t>(bytes[3]) << 24);

  };

  const bool validHeader = ReadFile(file, header, sizeof(header), &count, nullptr) &&

                           count == sizeof(header) && readLe32(header) == 0x48454258;

  if (validHeader) {

    const uint32_t base = readLe32(header + 0x104);

    const uint32_t certificate = readLe32(header + 0x118);

    const DWORD size = GetFileSize(file, nullptr);

    if (size != INVALID_FILE_SIZE && certificate >= base) {

      const uint32_t offset = certificate - base;

      if (offset <= size && size - offset >= 92 &&

          SetFilePointer(file, offset + 12, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {

        unsigned char name[80] = {};

        count = 0;

        if (ReadFile(file, name, sizeof(name), &count, nullptr)) {

          for (size_t i = 0; i + 1 < count; i += 2) {

            const unsigned value = name[i] | (static_cast<unsigned>(name[i + 1]) << 8);

            if (!value) break;

            title.push_back(value >= 32 && value < 127 ? static_cast<char>(value) : '?');

          }

        }

      }

    }

  }

  NtClose(file);

  while (!title.empty() && title.back() == ' ') title.pop_back();

  if (title.size() > 24) title.resize(24);

  return validHeader;

}

static std::string dvdDiagnostics = "DVD poll: waiting";

static std::mutex dvdStatusMutex;

static std::mutex dvdAccessMutex;

void lockDvdAccess() { dvdAccessMutex.lock(); }

void unlockDvdAccess() { dvdAccessMutex.unlock(); }

static std::string cachedDvdStatus = "DVD: Checking disc";

static HANDLE dvdPollStop = nullptr, dvdPollThread = nullptr;

std::string getDvdDiagnostics() {

  std::lock_guard<std::mutex> lock(dvdStatusMutex);

  return dvdDiagnostics;

}

static std::string dvdStatus() {

  ULONG smcValue = 0;

  const bool smcKnown = NT_SUCCESS(HalReadSMBusValue(0x20, 0x03, FALSE, &smcValue));

  ULONG kernelTray = 0xFFFFFFFF;

  HalReadSMCTrayState(&kernelTray, nullptr);

  const unsigned tray = smcValue & 0x70;

  const unsigned kernelState = kernelTray & 0x70;

  static unsigned previousTray = 0xFFFFFFFF;

  static unsigned previousKernelTray = 0xFFFFFFFF;

  static std::string gameLabel;

  static DWORD lastTitleAttempt = 0;

  const bool changed = (smcKnown ? tray : 0xFFFFFFFF) != previousTray ||

                       kernelState != previousKernelTray;

  if (changed) {

    gameLabel.clear();

    lastTitleAttempt = 0;

  }

  if (changed && ((smcKnown && tray == 0x60) || kernelState == 0x60)) {

    // A newly inserted disc must be mounted afresh. Otherwise a CDFS video

    // partition mounted during spin-up can hide the Xbox game filesystem.

    ANSI_STRING device;

    RtlInitAnsiString(&device, "\\Device\\CdRom0");

    const NTSTATUS mountStatus = IoDismountVolumeByName(&device);

    InfoLog::outputLine(InfoLog::DEBUG, "DVD media-change remount: %08lx\n", mountStatus);

  }

  previousTray = smcKnown ? tray : 0xFFFFFFFF;

  previousKernelTray = kernelState;

  const bool trayOpen = (smcKnown && tray == 0x10) || kernelState == 0x10;

  const DWORD now = GetTickCount();

  if (lastTitleAttempt == 0 || now - lastTitleAttempt >= 2000) {

    lastTitleAttempt = now;

    // Do not let a stale SMC 'empty' value suppress the actual disc check.

    // Reopen each time, so a removed or swapped game cannot retain its old title.

    std::string title;

    const bool gameReadable = dvdGameInfo(title);

    const DWORD titleError = gameReadable ? 0 : GetLastError();

    WIN32_FIND_DATAA entry = {};

    HANDLE directory = FindFirstFileA("D:\\*", &entry);

    const bool directoryReadable = directory != INVALID_HANDLE_VALUE;

    const DWORD directoryError = directoryReadable ? 0 : GetLastError();

    if (directoryReadable) FindClose(directory);

    char diagnostic[160] = {};

    snprintf(diagnostic, sizeof(diagnostic),

        "DVD poll %lu sec | SMC %02lx | kernel %02lx | XBE %lu | directory %lu",

        now / 1000, smcValue, kernelTray, titleError, directoryError);

    { std::lock_guard<std::mutex> lock(dvdStatusMutex); dvdDiagnostics = diagnostic; }

    if (gameReadable || directoryReadable) {

      gameLabel = title.empty() ? (gameReadable ? "DVD: Xbox game disc" : "DVD: Data disc") : "DVD: " + title;

    } else {

      gameLabel.clear();

    }

    if (changed) {

      InfoLog::outputLine(InfoLog::DEBUG, "DVD poll: SMC=%02lx kernel=%02lx readable=%u\n",

                          smcValue, kernelTray, gameLabel.empty() ? 0u : 1u);

    }

  }

  // A readable disc is stronger evidence than cached tray flags on patched BIOSes.

  if (!gameLabel.empty()) return gameLabel;

  if (trayOpen) return i18n::c("DVD: Tray open");

  if ((smcKnown && tray == 0x60) || kernelState == 0x60) return i18n::c("DVD: Disc inserted");

  if ((smcKnown && tray == 0x40) || kernelState == 0x40) return i18n::c("DVD: Empty");

  return i18n::c("DVD: Closing / checking");

}

// Keep disc spin-up and mounting off the controller/render thread.

static DWORD WINAPI pollDvdStatus(LPVOID) {

  while (WaitForSingleObject(dvdPollStop, 0) == WAIT_TIMEOUT) {

    std::string status;

    { std::lock_guard<std::mutex> access(dvdAccessMutex); status = dvdStatus(); }

    { std::lock_guard<std::mutex> lock(dvdStatusMutex); cachedDvdStatus = status; }

    if (WaitForSingleObject(dvdPollStop, 500) != WAIT_TIMEOUT) break;

  }

  return 0;

}

void resumeDvdStatusPolling() {

  if (dvdPollThread) return;

  dvdPollStop = CreateEvent(nullptr, TRUE, FALSE, nullptr);

  if (!dvdPollStop) return;

  dvdPollThread = CreateThread(nullptr, 0, pollDvdStatus, nullptr, 0, nullptr);

  if (!dvdPollThread) { CloseHandle(dvdPollStop); dvdPollStop = nullptr; }

}

void pauseDvdStatusPolling() {

  if (!dvdPollThread) return;

  SetEvent(dvdPollStop);

  // Finish outstanding DVD access before copying or launching a game.

  WaitForSingleObject(dvdPollThread, INFINITE);

  CloseHandle(dvdPollThread);

  CloseHandle(dvdPollStop);

  dvdPollThread = nullptr;

  dvdPollStop = nullptr;

}

static std::string readCachedDvdStatus() {

  std::lock_guard<std::mutex> lock(dvdStatusMutex);

  return cachedDvdStatus;

}

static void renderDvdCopyProgress(Menu& menu, Renderer& renderer, Font& font,

                                  double fraction, const std::string& elapsed,

                                  const std::string& note, bool confirmation) {

  renderer.clear();

  renderer.drawBackground();

  menu.render(font);

  const float panelWidth = renderer.getWidth() * 0.64f;

  const float panelHeight = 132.0f;

  const float panelX = (renderer.getWidth() - panelWidth) / 2.0f;

  const float panelY = renderer.getSafeBottom() - panelHeight;

  SDL_FRect panel = { panelX, panelY, panelWidth, panelHeight };

  renderer.setDrawColor(2, 11, 24, 245);

  renderer.fillRectangle(panel);

  SDL_Rect border = { static_cast<int>(panelX), static_cast<int>(panelY),

                      static_cast<int>(panelWidth), static_cast<int>(panelHeight) };

  renderer.setDrawColor(0, 174, 235, 255);

  SDL_RenderDrawRect(renderer.getRenderer(), &border);

  font.drawMenuText(confirmation ? i18n::c("Cancel DVD copy?") : i18n::c("Copying DVD game to HDD"),

      { panelX + 14.0f, panelY + 10.0f }, panelWidth - 28.0f, 0.88f, true);

  char percentage[80] = {};

  unsigned int percent = static_cast<unsigned int>(fraction * 100.0 + 0.5);

  if (percent > 100) percent = 100;

  snprintf(percentage, sizeof(percentage), i18n::c("%u%%   Elapsed: %s"), percent, elapsed.c_str());

  font.drawMenuText(percentage, { panelX + 14.0f, panelY + 38.0f }, panelWidth - 28.0f, 0.80f, true);

  const float barX = panelX + 14.0f;

  const float barY = panelY + 69.0f;

  const float barWidth = panelWidth - 28.0f;

  SDL_FRect track = { barX, barY, barWidth, 20.0f };

  renderer.setDrawColor(20, 38, 55, 255);

  renderer.fillRectangle(track);

  SDL_FRect progress = { barX, barY, barWidth * static_cast<float>(fraction), 20.0f };

  renderer.setDrawColor(0, 210, 255, 255);

  renderer.fillRectangle(progress);

  font.drawMenuText(note, { panelX + 14.0f, panelY + 96.0f },

                    panelWidth - 28.0f, 0.80f, true);

  renderer.flip();

}

static std::string temperatureStatus() {

  // The original Xbox exposes the CPU and board temperature through the SMC.

  // Some later board revisions do not provide these registers; in that case

  // keep the status panel honest and report the reading as unavailable.

  ULONG cpuTemperature = 0;

  ULONG boardTemperature = 0;

  NTSTATUS cpuStatus = HalReadSMBusValue(0x20, 0x09, FALSE, &cpuTemperature);

  NTSTATUS boardStatus = HalReadSMBusValue(0x20, 0x0A, FALSE, &boardTemperature);

  if (cpuStatus != 0 || boardStatus != 0 || cpuTemperature > 125 || boardTemperature > 125) {

    return i18n::c("TEMP: Not available");

  }

  char label[64] = {};

  snprintf(label, sizeof(label), i18n::c("TEMP: CPU %luC  BOARD %luC"),

           static_cast<unsigned long>(cpuTemperature),

           static_cast<unsigned long>(boardTemperature));

  return label;

}

static void renderGameArtwork(Menu& menu, Renderer& renderer, Font& font,

                              SDL_Texture*& cachedTexture, std::string& cachedGamePath) {

  MenuNode* current = menu.getCurrentMenu();

  if (!current) return;

  std::shared_ptr<MenuItem> selected = current->getSelectedItem();

  if (!selected) return;

  std::string xbePath = selected->getPreviewPath();

  if (xbePath.empty()) return;

  if (xbePath != cachedGamePath) {

    if (cachedTexture) SDL_DestroyTexture(cachedTexture);

    cachedTexture = nullptr;

    cachedGamePath = xbePath;

    size_t slash = xbePath.find_last_of("\\/");

    std::string folder = slash == std::string::npos ? std::string() : xbePath.substr(0, slash + 1);

    const char* artworkNames[] = { "default.tbn", "default.jpg", "folder.jpg", "default.png", "folder.png", "_resources/artwork/icon.png", "_resources/artwork/boxart.png" };

    for (const char* artworkName : artworkNames) {

      std::string artworkPath = folder + artworkName;

      if (GetFileAttributesA(artworkPath.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

      SDL_Surface* surface = IMG_Load(artworkPath.c_str());

      if (surface) {

        cachedTexture = SDL_CreateTextureFromSurface(renderer.getRenderer(), surface);

        SDL_FreeSurface(surface);

        break;

      }

    }

    if (!cachedTexture) {

      SDL_Surface* surface = XdashArtwork::fromXbe(xbePath);

      if (surface) {

        cachedTexture = SDL_CreateTextureFromSurface(renderer.getRenderer(), surface);

        SDL_FreeSurface(surface);

      }

    }

  }

  const int panelX = static_cast<int>(renderer.getWidth() * 0.08f);

  const int panelY = static_cast<int>(renderer.getHeight() * 0.61f);

  const int panelW = static_cast<int>(renderer.getWidth() * 0.43f);

  const int panelH = static_cast<int>(renderer.getHeight() * 0.30f);

  SDL_FRect panel = { static_cast<float>(panelX), static_cast<float>(panelY),

                      static_cast<float>(panelW), static_cast<float>(panelH) };

  renderer.setDrawColor(2, 11, 24, 220);

  renderer.fillRectangle(panel);

  SDL_Rect border = { panelX, panelY, panelW, panelH };

  renderer.setDrawColor(0, 174, 235, 210);

  SDL_RenderDrawRect(renderer.getRenderer(), &border);

  font.draw(std::string(selected->getLabel()), { panelX + 10.0f, panelY + 8.0f });

  SDL_Rect artworkRect = { panelX + 10, panelY + 42, panelW - 20, panelH - 52 };

  if (cachedTexture) {

    int imageW = 0;

    int imageH = 0;

    SDL_QueryTexture(cachedTexture, nullptr, nullptr, &imageW, &imageH);

    if (imageW > 0 && imageH > 0) {

      float scale = std::min(static_cast<float>(artworkRect.w) / imageW,

                             static_cast<float>(artworkRect.h) / imageH);

      artworkRect.w = static_cast<int>(imageW * scale);

      artworkRect.h = static_cast<int>(imageH * scale);

      artworkRect.x += (panelW - 20 - artworkRect.w) / 2;

      artworkRect.y += (panelH - 52 - artworkRect.h) / 2;

      SDL_RenderCopy(renderer.getRenderer(), cachedTexture, nullptr, &artworkRect);

    }

  } else {

    font.draw(i18n::c("No embedded icon or folder artwork available"),

              { panelX + 10.0f, panelY + 52.0f });

  }

}

#endif

// Area-filter once at the physical output size; software RenderCopy otherwise

// drops source pixels when shrinking the artwork. Premultiply alpha to avoid halos.

static SDL_Surface* filterSplash(SDL_Surface* source, int width, int height) {

  SDL_Surface* input = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0);

  SDL_Surface* output = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,

                                                      SDL_PIXELFORMAT_RGBA32);

  if (!input || !output) {

    if (input) SDL_FreeSurface(input);

    if (output) SDL_FreeSurface(output);

    return nullptr;

  }

  SDL_LockSurface(input);

  SDL_LockSurface(output);

  const double sx = double(input->w) / width, sy = double(input->h) / height;

  for (int y = 0; y < height; ++y) {

    const double top = y * sy, bottom = (y + 1) * sy;

    Uint8* dest = static_cast<Uint8*>(output->pixels) + y * output->pitch;

    for (int x = 0; x < width; ++x) {

      const double left = x * sx, right = (x + 1) * sx;

      double alpha = 0, red = 0, green = 0, blue = 0;

      for (int iy = int(top); iy < bottom && iy < input->h; ++iy) {

        const double wy = std::min(bottom, double(iy + 1)) - std::max(top, double(iy));

        const Uint8* row = static_cast<const Uint8*>(input->pixels) + iy * input->pitch;

        for (int ix = int(left); ix < right && ix < input->w; ++ix) {

          const double weight = wy * (std::min(right, double(ix + 1)) - std::max(left, double(ix)));

          const Uint8* pixel = row + ix * 4;

          const double a = pixel[3] * weight;

          alpha += a; red += pixel[0] * a; green += pixel[1] * a; blue += pixel[2] * a;

        }

      }

      dest[x*4] = alpha > 0 ? Uint8(red / alpha + 0.5) : 0;

      dest[x*4+1] = alpha > 0 ? Uint8(green / alpha + 0.5) : 0;

      dest[x*4+2] = alpha > 0 ? Uint8(blue / alpha + 0.5) : 0;

      dest[x*4+3] = Uint8(std::min(255.0, alpha / (sx * sy)) + 0.5);

    }

  }

  SDL_UnlockSurface(output);

  SDL_UnlockSurface(input);

  SDL_FreeSurface(input);

  return output;

}

int main(void) {

  // Capture diagnostics before mounting or selecting a video mode.

  InfoLog::capture();

#ifdef NXDK

  mountHomeDir('A');

#endif

  initializeDashboardPaths();

  initializeXisoKernel();

  Config config;

  // Keep boot and background diagnostics in the log rather than drawing them

  // over the dashboard artwork and menu.

  config.settings.logging.setOverlayEnabled(false);

  std::map<int, SDL_GameController*> controllers;

  InfoLog::configure(config);

  int init = init_systems(config);

  if (init) {

    shutdown_systems(init);

    return init;

  }

  InfoLog::capture();

  NetworkManager networkManager(config);

#ifdef NXDK

  if (config.settings.net.getEnabled()) {

    networkManager.asyncInit();

  }

#else

  networkManager.asyncInit();

#endif

  bool running = true;

  // Open our GameController

  for (int i = 0; i < SDL_NumJoysticks(); ++i) {

    if (SDL_IsGameController(i)) {

      controllers[i] = SDL_GameControllerOpen(i);

      if (!controllers[i]) {

        InfoLog::outputLine(InfoLog::ERROR, "Could not open gamecontroller %i: %s\n", i,

                            SDL_GetError());

        SDL_Delay(2000);

      }

    }

  }

  // Set a hint that we want to use our gamecontroller always

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

  // Create render system

  Renderer r;

  r.init(dashboardAssetPath("").c_str());

#ifdef NXDK

  r.setWidescreen((getEEPROMValue<uint32_t>(0x8) & 0x00010000) != 0);

#endif

  // Load font

  // FIXME: Font path should be read from theme

  Font f(r, dashboardAssetPath("Share-Bold.ttf").c_str());

#ifdef NXDK

  setDvdCopyProgressCallback(renderDvdCopyProgress, f);

#endif

  // Xbox-style boot: bring the centered XDash logo up from black, hold it

  // briefly, then fade it out before showing the live dashboard.

  const int logoSize = std::min(static_cast<int>(r.getHeight() * 0.70f),

                                static_cast<int>(r.getWidth() * 0.70f));

  SDL_Rect logoRect = { (r.getWidth() - logoSize) / 2,

                        (r.getHeight() - logoSize) / 2, logoSize, logoSize };

  float splashScaleX = 1, splashScaleY = 1;

  SDL_RenderGetScale(r.getRenderer(), &splashScaleX, &splashScaleY);

  SDL_Surface* splashSurface = IMG_Load(dashboardAssetPath("rich-retro-xdash-logo.png").c_str());

  SDL_Surface* filteredSplash = splashSurface ? filterSplash(splashSurface,

      std::max(1, int(logoSize * splashScaleX)),

      std::max(1, int(logoSize * splashScaleY))) : nullptr;

  SDL_Texture* splashLogo = splashSurface ? SDL_CreateTextureFromSurface(

      r.getRenderer(), filteredSplash ? filteredSplash : splashSurface) : nullptr;

  if (filteredSplash) SDL_FreeSurface(filteredSplash);

  if (splashSurface) SDL_FreeSurface(splashSurface);

  if (splashLogo) {

    SDL_SetTextureBlendMode(splashLogo, SDL_BLENDMODE_BLEND);

    for (int alpha = 0; alpha <= 255; alpha += 17) {

      r.setDrawColor(0, 0, 0, 255);

      r.clear();

      SDL_SetTextureAlphaMod(splashLogo, static_cast<Uint8>(alpha));

      SDL_RenderCopy(r.getRenderer(), splashLogo, nullptr, &logoRect);

      r.flip();

      SDL_Delay(30);

    }

    SDL_Delay(350);

    for (int alpha = 255; alpha >= 0; alpha -= 17) {

      r.setDrawColor(0, 0, 0, 255);

      r.clear();

      SDL_SetTextureAlphaMod(splashLogo, static_cast<Uint8>(alpha));

      SDL_RenderCopy(r.getRenderer(), splashLogo, nullptr, &logoRect);

      r.flip();

      SDL_Delay(30);

    }

    SDL_DestroyTexture(splashLogo);

  } else {

    InfoLog::outputLine(InfoLog::WARNING,

                        "Boot splash logo could not be loaded; continuing to dashboard.\n");

  }

  SubAppRouter& router = *SubAppRouter::getInstance();

  auto menu = std::make_shared<Menu>(config, r, networkManager);

  router.push(menu);

  std::shared_ptr<MenuNode> lang = nullptr;

  std::shared_ptr<MenuNode> timeZone = nullptr;

  r.drawBackground();

  r.flip();

  SDL_Event event;

#ifdef NXDK

  ULONG ValueIndex = 0x1;

  uint32_t Value = getEEPROMValue<uint32_t>(ValueIndex);

  if (Value == 0) {

#endif

    timeZone = std::make_shared<TimeMenu>(menu->getCurrentMenu(), "Timezone select");

    menu->setCurrentMenu(timeZone.get());

#ifdef NXDK

  }

  ValueIndex = 0x7;

  Value = getEEPROMValue<uint32_t>(ValueIndex);

  if (Value == 0) {

#endif

    lang = std::make_shared<LangMenu>(menu->getCurrentMenu(), "Language select");

    menu->setCurrentMenu(lang.get());

#ifdef NXDK

  }

#endif

  int info_x = static_cast<int>(r.getWidth() * 0.51);

  int info_y = 0;

  Uint32 lastStorageInfoUpdate = 0;

  Uint32 lastDvdStatusUpdate = 0;

  std::string liveDvdStatus = "DVD: Checking disc";

  std::vector<std::string> storageInfo;

  ftpServer* ftpServerInstance = nullptr;

  int runningFtpPort = -1;

  bool ftpStartAttempted = false;

  bool sntpStarted = false;

  std::shared_ptr<sntpClient> sntpClientInstance;

#ifdef NXDK

  SDL_Texture* gameArtworkTexture = nullptr;

  std::string cachedArtworkGamePath;

#endif

#ifdef NXDK

  resumeDvdStatusPolling();

#endif

  Uint32 lastDashboardDraw = 0;

  Uint32 lastFtpActivity = 0;

  bool ftpRecentlyActive = false;

  Uint32 lastFtpStartAttempt = 0;

  bool networkWasReady = false;

  auto lastFrameStart = std::chrono::steady_clock::now();

  while (running && !XBELauncher::hasPendingLaunch()) {

    auto frameStart = std::chrono::steady_clock::now();

    networkManager.pollConnection();

    if (networkManager.isNewlyInitialized()) {

    }

    const bool networkReady = networkManager.hasValidAddress();

    if (networkReady && !networkWasReady) {

      if (!ftpServerInstance) ftpStartAttempted = false;

      sntpStarted = false;

    }

    networkWasReady = networkReady;

    if (networkManager.getStatus() == NetworkManager::RUNNING && networkReady) {

      if (!ftpServerInstance && ftpStartAttempted &&

          static_cast<Uint32>(SDL_GetTicks() - lastFtpStartAttempt) >= 5000)

        ftpStartAttempted = false;

      const bool ftpEnabled = config.settings.ftp.getEnabled();

      const int configuredPort = config.settings.ftp.getPort();

      if (!ftpEnabled || (ftpServerInstance && runningFtpPort != configuredPort)) {

        delete ftpServerInstance;

        ftpServerInstance = nullptr;

        runningFtpPort = -1;

        ftpStartAttempted = false;

      }

      if (ftpEnabled && !ftpServerInstance && !ftpStartAttempted) {

        ftpStartAttempted = true;

        lastFtpStartAttempt = SDL_GetTicks();

        auto* candidate = new ftpServer(&config.settings.ftp);

        if (candidate->init() == 0) {

          candidate->runAsync();

          ftpServerInstance = candidate;

          runningFtpPort = configuredPort;

        } else {

          delete candidate;

        }

      }

    }

#ifdef NXDK

    sntpClient::setAutomaticEnabled(config.settings.sntp.getEnabled());

    if (!config.settings.sntp.getEnabled()) sntpStarted = false;

    if (!sntpStarted && config.settings.sntp.getEnabled() &&

        networkManager.hasValidAddress()) {

      sntpClientInstance = std::make_shared<sntpClient>(config.settings.sntp.getServer(),

                                                        config.settings.sntp.getPort());

      sntpClientInstance->updateTimeAsync();

      sntpStarted = true;

    }

#endif

    // Leave CPU time for disk/network workers during file transfers.

    // Input handling remains outside the redraw gate and runs every loop.

    const Uint32 drawTime = SDL_GetTicks();

    const bool transferActive = ftpServerInstance && ftpServerInstance->hasActiveTransfers();

    // Keep CPU available across the short command gaps between small files.

    if (transferActive) { lastFtpActivity = drawTime; ftpRecentlyActive = true; }

    if (ftpRecentlyActive && static_cast<Uint32>(drawTime - lastFtpActivity) >= 250)

      ftpRecentlyActive = false;

    const bool redraw = !ftpRecentlyActive || static_cast<Uint32>(drawTime - lastDashboardDraw) >= 66;

    if (redraw) {

    r.setDrawColor(0, 89, 0);

    r.clear();

    f.setSmoothing(config.settings.homescreen.getSmoothText());

    r.drawBackground();

#ifdef NXDK

    SYSTEMTIME localTime = {};

    GetLocalTime(&localTime);

    char dateLabel[40] = {};

    char timeLabel[32] = {};

    snprintf(dateLabel, sizeof(dateLabel), "%02u/%02u/%04u",

             static_cast<unsigned>(localTime.wDay),

             static_cast<unsigned>(localTime.wMonth),

             static_cast<unsigned>(localTime.wYear));

    snprintf(timeLabel, sizeof(timeLabel), "%02u:%02u:%02u",

             static_cast<unsigned>(localTime.wHour),

             static_cast<unsigned>(localTime.wMinute),

             static_cast<unsigned>(localTime.wSecond));

    const std::pair<float, float> clockPosition(r.getWidth() * 0.08f,

                                                 r.getHeight() * 0.08f);

    auto dateSize = f.draw(dateLabel, clockPosition);

    f.draw(timeLabel, { clockPosition.first, clockPosition.second + dateSize.second });

#endif

    router.render(f);

#ifdef NXDK

    renderGameArtwork(*menu, r, f, gameArtworkTexture, cachedArtworkGamePath);

#endif

    const Uint32 now = SDL_GetTicks();

#ifdef NXDK

    if (lastDvdStatusUpdate == 0 || static_cast<Uint32>(now - lastDvdStatusUpdate) >= 500) {

      liveDvdStatus = readCachedDvdStatus();

      lastDvdStatusUpdate = now;

    }

    if (lastStorageInfoUpdate == 0 || static_cast<Uint32>(now - lastStorageInfoUpdate) >= 2000) {

      storageInfo.clear();

      storageInfo.push_back(temperatureStatus());

      storageInfo.push_back(formatFreeSpace('C') + "   " + formatFreeSpace('E'));

      storageInfo.push_back(formatFreeSpace('F'));

      if (GetFileAttributesA("G:\\") != INVALID_FILE_ATTRIBUTES) {

        storageInfo.push_back(formatFreeSpace('G'));

      }

      storageInfo.push_back(liveDvdStatus);

      lastStorageInfoUpdate = now;

    }

    if (!storageInfo.empty()) storageInfo.back() = liveDvdStatus;

#endif

    std::pair<float, float> info_coordinates(info_x, info_y);

    if (menu->isAtMainMenu()) {

      const bool showIp = config.settings.homescreen.getShowIP();

      const float statusTextScale = 0.82f;

      const size_t statusLineCount = storageInfo.size() + (showIp ? 1 : 0);

      const float statusPanelWidth = static_cast<float>(r.getWidth()) * 0.46f;

      const float statusPanelHeight = f.getFontHeight() * statusTextScale * statusLineCount + 12.0f;

      const float statusPanelLeft = static_cast<float>(r.getWidth()) * 0.46f;

      const float statusPanelTop = static_cast<float>(r.getHeight()) * 0.89f - statusPanelHeight;

      info_x = static_cast<int>(statusPanelLeft + 8.0f);

      info_y = static_cast<int>(statusPanelTop + 6.0f);

      SDL_FRect statusPanel = { static_cast<float>(info_x - 12), statusPanelTop,

                                statusPanelWidth, statusPanelHeight };

      r.setDrawColor(2, 11, 24, 210);

      r.fillRectangle(statusPanel);

      SDL_Rect statusBorder = { static_cast<int>(statusPanel.x),

                                static_cast<int>(statusPanel.y),

                                static_cast<int>(statusPanel.w),

                                static_cast<int>(statusPanel.h) };

      r.setDrawColor(0, 174, 235, 170);

      SDL_RenderDrawRect(r.getRenderer(), &statusBorder);

      if (showIp) {

        std::string ip_address = networkManager.IPAddressString();

        ip_address = "IP: " + ip_address + std::string("   FTP: ") +

                     (config.settings.ftp.getEnabled() ? i18n::c("ON") : i18n::c("OFF"));

        auto draw_rect = f.drawMenuText(ip_address, info_coordinates, statusPanelWidth - 16.0f, statusTextScale, true);

        info_coordinates.second += draw_rect.second;

      }

#ifdef NXDK

      std::pair<float, float> status_coordinates(info_x, info_y);

      if (showIp) {

        status_coordinates.second += f.getFontHeight() * statusTextScale;

      }

      for (const auto& line : storageInfo) {

        auto draw_rect = f.drawMenuText(line, status_coordinates, statusPanelWidth - 16.0f, statusTextScale, true);

        status_coordinates.second += draw_rect.second;

      }

#endif

    }

    if (menu->isAtMainMenu()) {

      const std::string projectUrl = "https://github.com/RichsRetro";

      const float footerScale = 0.80f;

      const float footerLeft = static_cast<float>(r.getSafeLeft());

      const float footerY = static_cast<float>(r.getSafeBottom()) -

                            f.getFontHeight() * footerScale - 2.0f;

      f.drawMenuText(projectUrl, { footerLeft, footerY },

                     r.getSafeRight() - footerLeft, footerScale, true);

    }

    if (config.settings.homescreen.getShowFPS()) {

      int lastFrameDuration = static_cast<int>(millisBetween(lastFrameStart, frameStart));

      if (lastFrameDuration > 0) {

        int fps = 1000 / lastFrameDuration;

        char fpsBuffer[64] = { 0 };

        snprintf(fpsBuffer, 63, "FPS: %d (%dms)", fps, lastFrameDuration);

        f.draw(fpsBuffer, info_coordinates);

      }

    }

    InfoLog::renderOverlay(r, f);

    r.flip();

    lastDashboardDraw = SDL_GetTicks();

    } else {

      SDL_Delay(1);

    }

    menu->processScans();

    while (SDL_PollEvent(&event)) {

      if (event.type == SDL_QUIT) {

        running = false;

        break;

      } else if (event.type == SDL_CONTROLLERDEVICEADDED) {

        controllers[event.cdevice.which] = SDL_GameControllerOpen(event.cdevice.which);

      } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {

        SDL_GameControllerClose(controllers[event.cdevice.which]);

        controllers.erase(event.cdevice.which);

      } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {

        router.handleButtonDown(event.cbutton);

      } else if (event.type == SDL_CONTROLLERBUTTONUP) {

        router.handleButtonUp(event.cbutton);

      } else if (event.type == SDL_CONTROLLERAXISMOTION) {

        router.handleAxisMotion(event.caxis);

      }

    }

#ifdef NXDK

    // Let's not hog CPU for nothing.

    SwitchToThread();

#endif

    lastFrameStart = frameStart;

  }

#ifdef NXDK

  stopDvdCopyPreflight();

  pauseDvdStatusPolling();

#endif

  for (auto c: controllers) {

    SDL_GameControllerClose(c.second);

  }

  delete ftpServerInstance;

#ifdef NXDK

  if (gameArtworkTexture) SDL_DestroyTexture(gameArtworkTexture);

#endif

  if (XBELauncher::hasPendingLaunch()) {

    // All FTP workers have joined and closed their file/socket handles above.

    // Release controller, font, image and SDL resources before the quick reboot.

    IMG_Quit();

    TTF_Quit();

    SDL_Quit();

#ifdef NXDK

    // SDL only unregisters its controller callbacks; it leaves OHCI running.

    // Stop both DMA-capable drivers before their buffers become game memory.

    usbh_core_deinit();

    networkManager.stopForLaunch();

#endif

    XBELauncher::launchPending();

#ifdef NXDK

    // The renderer has been released; do not resume a dead event loop.

    XReboot();

#endif

  }

  shutdown_systems(init);

  return init;

}

