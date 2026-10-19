#include "xbeLauncher.hpp"

#include "infoLog.hpp"

#ifdef NXDK

#include <hal/video.h>

#include <hal/fileio.h>

#include <hal/xbox.h>

#include <xboxkrnl/xboxkrnl.h>

#endif

void XBELauncher::shutdown() {

#ifdef NXDK

  HalInitiateShutdown();

#endif

}

void XBELauncher::exitToDashboard() {

  showLaunchImage();

  // TODO: Switch to XLaunchXBE(nullptr) if XboxDev/nxdk#501 is merged.

  exit(0);

}

namespace { std::string pendingLaunchPath; }

bool XBELauncher::hasPendingLaunch() { return !pendingLaunchPath.empty(); }

void XBELauncher::launch(std::string const& xbePath) {

  // Leave the event handler before shutting down the dashboard's services.

  if (!xbePath.empty() && xbePath.size() < 240) pendingLaunchPath = xbePath;

}

void XBELauncher::launchPending() {

  const std::string xbePath = pendingLaunchPath;

  pendingLaunchPath.clear();

#ifdef NXDK

  // Retail titles access their assets through D:, including HDD-installed games.

  // Resolve the original path before remapping D: so DVD launches stay native.

  if (xbePath.empty() || xbePath.size() >= 240) {

    InfoLog::outputLine(InfoLog::ERROR, "Launch path is empty or too long.\n");

    return;

  }

  char nativePath[512] = {};

  if (XConvertDOSFilenameToXBOX(xbePath.c_str(), nativePath) != 0) {

    InfoLog::outputLine(InfoLog::ERROR, "Could not resolve launch path.\n");

    return;

  }

  std::string launchPath(nativePath);

  if (launchPath.compare(0, 17, "\\Device\\Harddisk0") == 0) {

    auto separator = launchPath.find_last_of('\\');

    if (separator == std::string::npos) return;

    std::string directory = launchPath.substr(0, separator);

    ANSI_STRING drive, target;

    RtlInitAnsiString(&drive, "\\??\\D:");

    RtlInitAnsiString(&target, directory.c_str());

    IoDeleteSymbolicLink(&drive);

    NTSTATUS status = IoCreateSymbolicLink(&drive, &target);

    if (!NT_SUCCESS(status)) {

      ANSI_STRING dvd;

      RtlInitAnsiString(&dvd, "\\Device\\CdRom0");

      IoCreateSymbolicLink(&drive, &dvd);

      InfoLog::outputLine(InfoLog::ERROR, "Could not map game directory to D: (%08lx).\n", (unsigned long)status);

      return;

    }

  }

  showLaunchImage();

  XLaunchXBE(launchPath.c_str());

  // A failed launch must return to a visible dashboard rather than a black screen.

  XVideoSetVideoEnable(TRUE);

  InfoLog::outputLine(InfoLog::ERROR, "The Xbox returned without launching the executable.\n");

#endif

}

void XBELauncher::showLaunchImage() {

#ifdef NXDK

  VIDEO_MODE mode = XVideoGetMode();

  // TODO(XboxDev/nxdk#507): Display launch image instead when framebuffer can be persisted.

  unsigned char* fb = XVideoGetFB();

  memset(fb, 0, mode.width * mode.height * (mode.bpp >> 3));

  XVideoFlushFB();

  XVideoSetVideoEnable(FALSE);

#endif

}

