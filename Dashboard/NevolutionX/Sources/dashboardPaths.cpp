#include "dashboardPaths.hpp"

#include <algorithm>

#ifdef NXDK

#include <windows.h>

#endif

namespace { std::string rootPath; bool structured = false; }

void initializeDashboardPaths() {

#ifdef NXDK

  rootPath = "A:\\";

  if (GetFileAttributesA("A:\\assets") != INVALID_FILE_ATTRIBUTES) {

    structured = true;

  } else if (GetFileAttributesA("A:\\RRXD\\assets") != INVALID_FILE_ATTRIBUTES) {

    rootPath += "RRXD\\";

    structured = true;

  }

#else

  rootPath = "./";

#endif

}

std::string dashboardAssetPath(const char* name) {

  return rootPath + (structured ? "assets\\" : "") + name;

}

std::string dashboardConfigPath() {

  return rootPath + (structured ? "config\\config.json" : "config.json");

}

std::string dashboardDataPath(const char* name) {

  std::string path = rootPath + name;

#ifdef NXDK

  std::replace(path.begin(), path.end(), '/', '\\');

#endif

  return path;

}

