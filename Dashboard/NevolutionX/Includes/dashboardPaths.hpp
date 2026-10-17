#pragma once
#include <string>

// A: is NXDK's mapping of the running XBE's directory. Support both the
// standalone RRXD/default.xbe and the C-root BIOS boot copy, without a reboot.
void initializeDashboardPaths();
std::string dashboardAssetPath(const char* name);
std::string dashboardConfigPath();

std::string dashboardDataPath(const char* name);
