#ifndef MENU_HPP

#define MENU_HPP

#include <list>

#include <memory>

#include <mutex>

#include <string>

#include <vector>

#include "config.hpp"

#include "font.hpp"

#include "subApp.hpp"

#include "xbeScanner.hpp"

class MenuNode;

class Menu;

class NetworkManager;

class MenuItem {

public:

  explicit MenuItem(std::string const& label);

  MenuItem(MenuNode* parent, std::string const& label);

  virtual ~MenuItem();

  virtual void execute(Menu*) = 0;

  virtual bool isSelectable() const { return true; }

  virtual float getTextScale() const { return 1.0f; }

  virtual void executeStart(Menu* menu) { execute(menu); }

  MenuNode* getParent() const;

  virtual std::string_view getLabel() const;

  virtual std::string getPreviewPath() const { return {}; }

  void setParent(MenuNode* parent);

protected:

  MenuNode* parentNode;

  const std::string label;
  mutable std::string translatedLabel;

};

class MenuNode : public MenuItem {

public:

  explicit MenuNode(std::string const& label);

  MenuNode(MenuNode* parent, std::string const& label);

  ~MenuNode() override;

  void execute(Menu*) override;

  // Optional renderer for bounded file-manager lists; default menus are unchanged.

  virtual bool renderCustomMenu(Font&, Renderer&) { return false; }

  void setSelected(size_t id);

  size_t getSelected();

  virtual std::vector<std::shared_ptr<MenuItem>>* getChildNodes();

  std::shared_ptr<MenuItem> getSelectedItem() const;

  void addNode(std::shared_ptr<MenuItem> node);

  void up();

  void down();

  void pageUp(int pageSize);

  void pageDown(int pageSize);

  virtual void superscrollUp() {}

  virtual void superscrollDown() {}

protected:

  void moveSelection(int delta, bool allowWrap = true);

  size_t selected = 0;

  std::vector<std::shared_ptr<MenuItem>> childNodes;

};

// A non-interactive menu entry that simply displays a label.

class MenuNonInteractive : public MenuNode {

public:

  MenuNonInteractive(MenuNode* parent, std::string const& label);

  void execute(Menu*) override;

  bool isSelectable() const override { return false; }

  float getTextScale() const override { return 0.72f; }

};

class MenuXbe : public MenuNode {

public:

  MenuXbe(MenuNode* parent, std::string const& label, std::string const& paths);

  ~MenuXbe() override;

  void execute(Menu* menu) override;

  void addStaticNode(std::shared_ptr<MenuItem> node);

  void refresh();

  void processScan();

  std::string getScanStatus() const;

  std::vector<std::shared_ptr<MenuItem>>* getChildNodes() override;

  void superscrollUp() override { superscroll(true); }

  void superscrollDown() override { superscroll(false); }

private:

  void superscroll(bool moveToPrevious);

  void updateScanningLabel();

  void onScanCompleted(bool succeeded,

                       std::list<XBEScanner::XBEInfo> const& items,

                       long long duration);

  void createChildren();

  void startScan();

  std::mutex childNodesLock;

  std::list<std::string> scanPaths;

  std::list<std::string> remainingScanPaths;

  std::vector<XBEScanner::XBEInfo> discoveredItems;

  std::vector<std::shared_ptr<MenuItem>> staticChildren;

  bool scanStarted{ false };

  // Map of first letter to index of the first child in childNodes whose label starts with

  // that letter.

  std::map<int, size_t> superscrollIndex;

};

class MenuLaunch : public MenuItem {

public:

  std::string_view getLabel() const override { return translateLabel ? MenuItem::getLabel() : std::string_view(label); }

  MenuLaunch(std::string const& label, std::string const& path, bool translateLabel = true);

  ~MenuLaunch() override;

  void execute(Menu*) override;

  std::string getPreviewPath() const override { return path; }

protected:

  std::string path;
  bool translateLabel;

};

class MenuExec : public MenuItem {

public:

  MenuExec(std::string const& label, void execute(Menu*));

  ~MenuExec() override;

  void execute(Menu*) override;

  void (*action)(Menu*);

};

class Menu : public SubApp {

public:

  Menu(Config& config, Renderer& renderer, NetworkManager& networkManager);

  void render(Font& font) override;

  MenuNode* getCurrentMenu();

  bool isAtMainMenu() const { return currentMenu == &rootNode; }

  void setCurrentMenu(MenuNode*);

  void refreshLists();

  void refreshAfterDvdCopy() { dvdRefreshPending = true; }

  void processScans();

  inline Renderer& getRenderer() { return renderer; }

  void onUpPressed() override;

  void onDownPressed() override;

  void onLeftPressed() override { pageUp(); }

  void onRightPressed() override { pageDown(); }

  void onAPressed() override { execute(); }

  void onStartPressed() override;

  void onBackPressed() override { back(); }

  void onBPressed() override { back(); }

  void onLeftStickDigitalUpPressed() override { onUpPressed(); }

  void onLeftStickDigitalDownPressed() override { onDownPressed(); }

  void onLeftStickDigitalLeftPressed() override { onLeftPressed(); }

  void onLeftStickDigitalRightPressed() override { onRightPressed(); }

  void onRightStickDigitalUpPressed() override { superscrollUp(); }

  void onRightStickDigitalDownPressed() override { superscrollDown(); }

  void pageUp();

  void pageDown();

  void superscrollUp();

  void superscrollDown();

  void back();

  void execute();

protected:

  Renderer& renderer;

  NetworkManager& networkManager;

  std::vector<std::shared_ptr<MenuXbe>> scanMenus;

  int refreshNoticeFrames{ 0 };

  bool dvdRefreshPending{ false };

  MenuNode rootNode;

  MenuNode* currentMenu;

  int menuHeight;

  int startHeight;

  size_t offsetForDraw = 0;

  size_t itemsToShow = 16;

  size_t lowerHalf = 9;

  size_t upperHalf = 7;

};

#endif

