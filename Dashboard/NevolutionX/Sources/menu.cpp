#include "menu.hpp"
#include <memory>
#include "3rdparty/NaturalSort/natural_sort.hpp"
#include "infoLog.hpp"
#include "settingsMenu.hpp"
#include "fileExplorer.hpp"
#include "dvdCopy.hpp"
#include "xbeLauncher.hpp"
#include "xbeScanner.hpp"
#include "networkManager.hpp"

// Character used in the config.json to separate multiple path entries.
#define PATH_DELIMITER ','

/******************************************************************************************
                                   MenuItem
******************************************************************************************/
MenuItem::MenuItem(std::string const& label) : label(label) {
}

MenuItem::MenuItem(MenuNode* parent, std::string const& label) :
    parentNode(parent), label(label) {
}

MenuItem::~MenuItem() {
}

std::string_view MenuItem::getLabel() const {
  return this->label;
}

MenuNode* MenuItem::getParent() const {
  return parentNode;
}

void MenuItem::setParent(MenuNode* parent) {
  parentNode = parent;
}

/******************************************************************************************
                                   MenuNode
******************************************************************************************/

MenuNode::MenuNode(std::string const& label) : MenuItem(label) {
}

MenuNode::MenuNode(MenuNode* parent, std::string const& label) : MenuItem(label) {
  this->setParent(parent);
}

MenuNode::~MenuNode() {
}

void MenuNode::execute(Menu* menu) {
  if (menu->getCurrentMenu() != this) {
    menu->setCurrentMenu(this);
  } else {
    if (childNodes.size() == 0) {
      return;
    }
    if (childNodes.size() > selected) {
      this->childNodes.at(selected)->execute(menu);
    }
  }
}

void MenuNode::setSelected(size_t id) {
  if (id >= childNodes.size()) return;
  for (size_t i = id; i < childNodes.size(); ++i) {
    if (childNodes[i]->isSelectable()) { selected = i; return; }
  }
  for (size_t i = id; i > 0; --i) {
    if (childNodes[i - 1]->isSelectable()) { selected = i - 1; return; }
  }
}

size_t MenuNode::getSelected() {
  return this->selected;
}

std::vector<std::shared_ptr<MenuItem>>* MenuNode::getChildNodes() {
  return &this->childNodes;
}

std::shared_ptr<MenuItem> MenuNode::getSelectedItem() const {
  if (selected >= childNodes.size()) return nullptr;
  return childNodes[selected];
}

void MenuNode::addNode(std::shared_ptr<MenuItem> node) {
  bool hadSelectable = false;
  for (const auto& child : childNodes) {
    if (child->isSelectable()) { hadSelectable = true; break; }
  }
  const size_t newIndex = childNodes.size();
  const bool selectable = node->isSelectable();
  this->childNodes.push_back(node);
  if (selectable && !hadSelectable) selected = newIndex;
}

void MenuNode::up() {
  moveSelection(-1);
}

void MenuNode::down() {
  moveSelection(1);
}

void MenuNode::pageUp(int pageSize) {
  moveSelection(-pageSize, false);
}

void MenuNode::pageDown(int pageSize) {
  moveSelection(pageSize, false);
}

void MenuNode::moveSelection(int delta, bool allowWrap) {
  if (childNodes.empty() || !delta) {
    return;
  }
  const size_t numNodes = childNodes.size();
  const int direction = delta > 0 ? 1 : -1;
  int remaining = delta > 0 ? delta : -delta;
  while (remaining > 0) {
    size_t candidate = selected;
    bool found = false;
    for (size_t checked = 0; checked < numNodes; ++checked) {
      if (direction > 0) {
        if (candidate + 1 >= numNodes) {
          if (!allowWrap) break;
          candidate = 0;
        } else {
          ++candidate;
        }
      } else {
        if (candidate == 0) {
          if (!allowWrap) break;
          candidate = numNodes - 1;
        } else {
          --candidate;
        }
      }
      if (childNodes[candidate]->isSelectable()) {
        found = true;
        break;
      }
    }
    if (!found) return;
    selected = candidate;
    --remaining;
  }
}

/******************************************************************************************
                               MenuNonInteractive
******************************************************************************************/
MenuNonInteractive::MenuNonInteractive(MenuNode* parent, std::string const& label) :
    MenuNode(parent, label) {
}

void MenuNonInteractive::execute(Menu*) {
  // Do nothing, this node cannot be entered.
}

/******************************************************************************************
                                   MenuXbe
******************************************************************************************/
MenuXbe::MenuXbe(MenuNode* parent, std::string const& label, std::string const& paths) :
    MenuNode(parent, label) {

  size_t path_start = 0;
  for (size_t path_end = paths.find(PATH_DELIMITER, path_start);
       path_end != std::string::npos; path_end = paths.find(PATH_DELIMITER, path_start)) {

    remainingScanPaths.emplace_back(paths.substr(path_start, path_end - path_start));
    path_start = path_end + 1;
  }
  remainingScanPaths.emplace_back(paths.substr(path_start));
  scanPaths = remainingScanPaths;

}

MenuXbe::~MenuXbe() {
}

void MenuXbe::execute(Menu* menu) {
  if (menu->getCurrentMenu() != this) {
    menu->setCurrentMenu(this);
    startScan();
  } else {
    if (childNodes.size() > selected) {
      this->childNodes.at(selected)->execute(menu);
    }
  }
}

void MenuXbe::addStaticNode(std::shared_ptr<MenuItem> node) {
  staticChildren.push_back(node);
  MenuNode::addNode(std::move(node));
}

void MenuXbe::startScan() {
  if (!scanStarted) refresh();
}

void MenuXbe::refresh() {
  if (scanStarted && !remainingScanPaths.empty()) return;
  scanStarted = true;
  remainingScanPaths = scanPaths;
  discoveredItems.clear();
  superscrollIndex.clear();
  selected = 0;
  updateScanningLabel();
}

std::string MenuXbe::getScanStatus() const {
  if (!scanStarted || remainingScanPaths.empty()) return {};
  return "Scanning " + std::string(getLabel()) + ": " + remainingScanPaths.front();
}

void MenuXbe::processScan() {
  if (!scanStarted || remainingScanPaths.empty()) return;
  XBEScanner::scanPath(remainingScanPaths.front(),
      [this](bool succeeded, std::list<XBEScanner::XBEInfo> const& items,
             long long duration) { onScanCompleted(succeeded, items, duration); });
}
std::vector<std::shared_ptr<MenuItem>>* MenuXbe::getChildNodes() {
  std::lock_guard<std::mutex> lock(childNodesLock);
  return MenuNode::getChildNodes();
}

void MenuXbe::superscroll(bool moveToPrevious) {
  std::lock_guard<std::mutex> lock(childNodesLock);

  if (childNodes.empty() || superscrollIndex.empty()) {
    return;
  }

  const auto& currentItemLabel = childNodes[selected]->getLabel();
  int firstChar = tolower(currentItemLabel[0]);

  auto it = superscrollIndex.find(firstChar);
  if (it == superscrollIndex.end()) {
    InfoLog::outputLine(InfoLog::ERROR, "Failed to find %d (%c) in superscroll index",
                        firstChar, static_cast<char>(firstChar));
    return;
  }

  if (moveToPrevious) {
    if (it == superscrollIndex.begin()) {
      return;
    }

    --it;
  } else {
    ++it;
    if (it == superscrollIndex.end()) {
      return;
    }
  }

  selected = it->second;
}

void MenuXbe::updateScanningLabel() {

  std::lock_guard<std::mutex> lock(childNodesLock);
  childNodes.clear();
  for (const auto& child : staticChildren) childNodes.push_back(child);
}

void MenuXbe::onScanCompleted(bool succeeded,
                              std::list<XBEScanner::XBEInfo> const& items,
                              long long duration) {
  (void)duration;
  std::string path = remainingScanPaths.front();
  remainingScanPaths.pop_front();

  if (!succeeded) {
    InfoLog::outputLine(InfoLog::WARNING, "Failed to scan '%s' for XBEs, skipping...\n",
                        path.c_str());
  } else {
    discoveredItems.insert(discoveredItems.end(), std::make_move_iterator(begin(items)),
                           std::make_move_iterator(end(items)));
  }

  if (!remainingScanPaths.empty()) return;

  createChildren();
}

void MenuXbe::createChildren() {
  std::vector<std::shared_ptr<MenuItem>> newChildren;

  for (auto& info: discoveredItems) {
    newChildren.push_back(std::make_shared<MenuLaunch>(info.name, info.path));
  }
  for (const auto& child : staticChildren) newChildren.push_back(child);

  std::sort(begin(newChildren), end(newChildren),
            [](const std::shared_ptr<MenuItem>& a, const std::shared_ptr<MenuItem>& b) {
              return SI::natural::compare(a->getLabel(), b->getLabel());
            });

  std::map<int, size_t> index;
  int lastProcessed = -1;
  for (size_t i = 0; i < newChildren.size(); ++i) {
    const auto& childLabel = newChildren[i]->getLabel();

    if (childLabel.empty()) {
      InfoLog::outputLine(InfoLog::WARNING,
                          "Child with empty label while building superscroll index.");
      continue;
    }

    int firstChar = tolower(childLabel[0]);
    if (firstChar == lastProcessed) {
      continue;
    }

    index[firstChar] = i;
    lastProcessed = firstChar;
  }

  std::lock_guard<std::mutex> lock(childNodesLock);
  childNodes = newChildren;
  selected = 0;
  superscrollIndex = index;
}


/******************************************************************************************
                                   MenuLaunch
******************************************************************************************/
MenuLaunch::MenuLaunch(std::string const& label, std::string const& path) :
    MenuItem(label), path(path) {
}

MenuLaunch::~MenuLaunch() {
}

void MenuLaunch::execute(Menu*) {
  InfoLog::outputLine(InfoLog::DEBUG, "Launching xbe %s\n", this->path.c_str());
#ifdef NXDK
  std::string usePath = path;
  if (path[0] == 'D') {
    usePath.replace(0, 2, "\\Device\\CdRom0");
  }
  XBELauncher::launch(usePath);
#endif
}

/******************************************************************************************
                                   MenuExec
******************************************************************************************/
MenuExec::MenuExec(std::string const& label, void execute(Menu*)) :
    MenuItem(label), action(execute) {
}

MenuExec::~MenuExec() {
}

void MenuExec::execute(Menu* menu) {
  action(menu);
}
/******************************************************************************************
                                   Menu
******************************************************************************************/
Menu::Menu(Config& config, Renderer& renderer, NetworkManager& networkManager) :
    renderer(renderer), networkManager(networkManager), rootNode("Main menu") {
  rootNode.setParent(&rootNode);
  currentMenu = &rootNode;
  menuHeight = renderer.getSafeBottom();
  startHeight = std::max(renderer.getSafeTop() + 62,
                         static_cast<int>(renderer.getHeight() * 0.18f));
  for (nlohmann::json const& e: config.menu) {
    if (!static_cast<std::string>(e["type"]).compare("scan")) {
      std::shared_ptr<MenuXbe> newNode = std::make_shared<MenuXbe>(currentMenu, e["label"],
                                                                   e["path"]);
      scanMenus.push_back(newNode);
      this->rootNode.addNode(newNode);
    } else if (!static_cast<std::string>(e["type"]).compare("launch")) {
      std::shared_ptr<MenuLaunch> newNode = std::make_shared<MenuLaunch>(e["label"],
                                                                         e["path"]);
      this->rootNode.addNode(newNode);
    } else if (!static_cast<std::string>(e["type"]).compare("reboot")) {
      std::shared_ptr<MenuExec> newNode = std::make_shared<MenuExec>(
          e["label"], [](Menu*) { XBELauncher::exitToDashboard(); });
      this->rootNode.addNode(newNode);
    } else if (!static_cast<std::string>(e["type"]).compare("shutdown")) {
      std::shared_ptr<MenuExec> newNode = std::make_shared<MenuExec>(
          e["label"], [](Menu*) { XBELauncher::shutdown(); });
      this->rootNode.addNode(newNode);
    } else if (!static_cast<std::string>(e["type"]).compare("refresh")) {
      rootNode.addNode(std::make_shared<MenuExec>(e["label"],
          [](Menu* menu) { menu->refreshLists(); }));
    } else if (!static_cast<std::string>(e["type"]).compare("settings")) {
      std::shared_ptr<MenuNode> newNode = std::make_shared<settingsMenu>(
          currentMenu, e["label"], config, networkManager);
      this->rootNode.addNode(newNode);
    } else if (!static_cast<std::string>(e["type"]).compare("explorer")) {
      std::shared_ptr<FileExplorerDirectory> newNode =
          std::make_shared<FileExplorerDirectory>(currentMenu, e["label"], "");
      this->rootNode.addNode(newNode);
    } else if (!static_cast<std::string>(e["type"]).compare("dvdcopy")) {
      std::shared_ptr<DvdCopyMenu> newNode =
          std::make_shared<DvdCopyMenu>(currentMenu, e["label"]);
      this->rootNode.addNode(newNode);
    } else if (!static_cast<std::string>(e["type"]).compare("appsfolder")) {
      std::shared_ptr<MenuXbe> applications =
          std::make_shared<MenuXbe>(currentMenu, e["label"], e["path"]);
      applications->addStaticNode(std::make_shared<DvdCopyMenu>(
          applications.get(), "Copy DVD to HDD"));
      scanMenus.push_back(applications);
      this->rootNode.addNode(applications);
    }
  }

  // TODO: Take the repeat interval from the config.
  autoRepeatIntervals[SDL_CONTROLLER_BUTTON_DPAD_UP] = 250;
  autoRepeatIntervals[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = 250;
}

void Menu::refreshLists() {
  for (const auto& entry : scanMenus) entry->refresh();
  refreshNoticeFrames = 180;
}

void Menu::processScans() {
  if (dvdRefreshPending) {
    bool scanning = false;
    for (const auto& entry : scanMenus) if (!entry->getScanStatus().empty()) scanning = true;
    if (!scanning) { dvdRefreshPending = false; refreshLists(); }
  }
  // One location per frame: draw the small status before doing filesystem work.
  for (const auto& entry : scanMenus) {
    if (!entry->getScanStatus().empty()) { entry->processScan(); return; }
  }
}

void Menu::render(Font& font) {
  if (currentMenu->renderCustomMenu(font, renderer)) return;
  std::string scanStatus;
  for (const auto& entry : scanMenus) {
    scanStatus = entry->getScanStatus();
    if (!scanStatus.empty()) break;
  }
  if (scanStatus.empty() && refreshNoticeFrames > 0) {
    scanStatus = "Games & Apps refreshed";
    --refreshNoticeFrames;
  }
  if (!scanStatus.empty()) {
    font.drawColumnScaled(scanStatus,
        { static_cast<float>(renderer.getSafeLeft()),
          renderer.getSafeBottom() - font.getFontHeight() * 1.5f },
        renderer.getWidth() * 0.38f, 0.60f);
  }
  int menuX = std::max(renderer.getSafeLeft(),
                       static_cast<int>(renderer.getWidth() * 0.63f));
  const int safeRight = renderer.getSafeRight();
  const int safeBottom = renderer.getSafeBottom();
  const float maxTextWidth = static_cast<float>(std::max(1, safeRight - menuX - 12));
  std::pair<float, float> coordinates(menuX, startHeight);
  std::string menutext = std::string(this->currentMenu->getLabel());
  font.drawColumnScaled(menutext, std::make_pair(
      static_cast<float>(menuX), static_cast<float>(std::max(
          renderer.getSafeTop(), static_cast<int>(renderer.getHeight() * 0.1f)))),
      maxTextWidth, 1.10f);

  const float rowHeight = std::max(font.getFontHeight() * 0.88f, 1.0f);
  menuHeight = safeBottom;
  itemsToShow = static_cast<size_t>(std::max(1.0f,
      (static_cast<float>(safeBottom) - coordinates.second) / rowHeight));
  lowerHalf = itemsToShow / 2;
  upperHalf = itemsToShow - lowerHalf;

  if (this->currentMenu->getChildNodes()->empty()) {
    font.drawMenuText("<Empty>", coordinates, maxTextWidth, 0.88f, true);
    return;
  }

  const auto& children = *currentMenu->getChildNodes();
  const size_t selected = std::min(currentMenu->getSelected(), children.size() - 1);
  const float availableHeight = std::max(1.0f, safeBottom - coordinates.second);
  auto scaleForRow = [](const std::shared_ptr<MenuItem>& item) {
    return item->isSelectable() ? item->getTextScale() * 0.78f + 0.10f
        : std::max(0.80f, item->getTextScale() * 0.78f + 0.10f);
  };
  auto heightForRow = [&](size_t index) {
    return std::max(1.0f, font.measureMenuText(std::string(children[index]->getLabel()),
        maxTextWidth, scaleForRow(children[index])).second);
  };
  // Position the selected row using the same wrapped font metrics used to draw it.
  // An estimated row count can stop scrolling early when the final rows wrap.
  offsetForDraw = selected;
  float usedHeight = heightForRow(selected);
  while (offsetForDraw > 0) {
    const float previousHeight = heightForRow(offsetForDraw - 1);
    if (usedHeight + previousHeight > availableHeight * 0.5f) break;
    usedHeight += previousHeight;
    --offsetForDraw;
  }
  size_t visibleEnd = selected + 1;
  while (visibleEnd < children.size()) {
    const float nextHeight = heightForRow(visibleEnd);
    if (usedHeight + nextHeight > availableHeight) break;
    usedHeight += nextHeight;
    ++visibleEnd;
  }
  // At the end of the list, fill spare space above without hiding the selection.
  if (visibleEnd == children.size()) {
    while (offsetForDraw > 0) {
      const float previousHeight = heightForRow(offsetForDraw - 1);
      if (usedHeight + previousHeight > availableHeight) break;
      usedHeight += previousHeight;
      --offsetForDraw;
    }
  }
  itemsToShow = std::max(size_t(1), visibleEnd - offsetForDraw);

  size_t i = offsetForDraw;
  for (auto it = begin(*this->currentMenu->getChildNodes()) + offsetForDraw;
       it != end(*this->currentMenu->getChildNodes()) && i < visibleEnd; ++it) {
    menutext = std::string((*it)->getLabel());
    std::pair<float, float> dimensions;
    const float textScale = (*it)->isSelectable()
        ? (*it)->getTextScale() * 0.78f + 0.10f
        : std::max(0.80f, (*it)->getTextScale() * 0.78f + 0.10f);
    dimensions = font.drawMenuText(menutext, coordinates, maxTextWidth,
                                   textScale, !(*it)->isSelectable(),
                                   i == currentMenu->getSelected() && (*it)->isSelectable());

    if (i == this->currentMenu->getSelected() && (*it)->isSelectable()) {
      SDL_Rect rect;
      rect.w = std::get<0>(dimensions) + 20;
      rect.h = std::get<1>(dimensions);
      rect.x = std::get<0>(coordinates) - 10;
      rect.y = std::get<1>(coordinates);
      renderer.setDrawColor(0, 175, 255, 55);
      SDL_RenderFillRect(renderer.getRenderer(), &rect);
      renderer.setDrawColor(0, 215, 255, 255);
      SDL_RenderDrawRect(renderer.getRenderer(), &rect);
    }

    coordinates = std::pair<float, float>(
        std::get<0>(coordinates), std::get<1>(coordinates) + std::get<1>(dimensions));
    if (std::get<1>(coordinates) > menuHeight) {
      break;
    }
    ++i;
  }
}

MenuNode* Menu::getCurrentMenu() {
  return currentMenu;
}

void Menu::setCurrentMenu(MenuNode* menu) {
  currentMenu = menu;
}

void Menu::onUpPressed() {
  currentMenu->up();
}

void Menu::onDownPressed() {
  currentMenu->down();
}

void Menu::pageUp() {
  currentMenu->pageUp(static_cast<int>(itemsToShow));
}

void Menu::pageDown() {
  currentMenu->pageDown(static_cast<int>(itemsToShow));
}

void Menu::superscrollUp() {
  currentMenu->superscrollUp();
}

void Menu::superscrollDown() {
  currentMenu->superscrollDown();
}

void Menu::back() {
  InfoLog::outputLine(InfoLog::DEBUG, "Setting menu to %s\n",
                      std::string(currentMenu->getParent()->getLabel()).c_str());
  currentMenu = currentMenu->getParent();
}

void Menu::execute() {
  this->currentMenu->execute(this);
}

void Menu::onStartPressed() {
  std::shared_ptr<MenuItem> selected = currentMenu->getSelectedItem();
  if (selected) selected->executeStart(this);
  else currentMenu->execute(this);
}
