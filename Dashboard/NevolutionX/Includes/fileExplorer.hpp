#ifndef RICH_RETRO_FILE_EXPLORER_HPP
#define RICH_RETRO_FILE_EXPLORER_HPP

#include "menu.hpp"

class FileExplorerDirectory : public MenuNode {
public:
  FileExplorerDirectory(MenuNode* parent, const std::string& label,
                        const std::string& path);
  void execute(Menu* menu) override;
  void executeStart(Menu* menu) override;
  void reloadOnNextOpen();
  bool renderCustomMenu(Font& font, Renderer& renderer) override;

private:
  void loadChildren();
  std::string path;
  bool loaded{ false };
  std::shared_ptr<MenuNode> actionMenu;
};

#endif
