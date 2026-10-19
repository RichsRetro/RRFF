#pragma once

#include <cstdint>

#include <string>

#include <vector>

class GamePatchPlan {

public:

  bool prepare(const std::string& sourceRoot, std::string& error);

  bool skipFile(const std::string& relative) const;

  bool validateName(const std::string& relative, std::string& error) const;

  std::string destinationName(const std::string& relative) const;

  bool apply(const std::string& destinationRoot, std::string& error) const;

  std::string description() const;

private:

  struct Rename { std::string from, to; };

  struct Edit { uint32_t offset; std::vector<unsigned char> before, after; };

  struct File { std::string path, hash; std::vector<Edit> edits; };

  std::vector<Rename> renames;

  std::vector<File> files;

  std::vector<std::string> excluded;

  std::string label;

  std::string mapPath(std::string relative) const;

};

