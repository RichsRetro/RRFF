#include "i18n.hpp"

#include "fileExplorer.hpp"

#include <algorithm>

#include <cctype>

#include <cstring>

#include <functional>

#include <mutex>

#include <cstdlib>

#include <exception>

#include "subAppRouter.hpp"

#include <string>

#include <vector>

#ifdef NXDK

#include <windows.h>

#include <xboxkrnl/xboxkrnl.h>

#include "xbeLauncher.hpp"

#endif

namespace {

#ifdef NXDK

std::string clipboardPath;

bool clipboardIsMove = false;

enum class FileOperation { Copy, Move, Delete };

struct OperationState {

  std::mutex mutex;

  char* copyBuffer{nullptr};

  ~OperationState() { free(copyBuffer); }

  std::string current, phase{i18n::c("Calculating totals...")}, error;

  unsigned long long files{0}, folders{0}, totalBytes{0};

  unsigned long long filesDone{0}, bytesDone{0}, workDone{0}, totalWork{0};

  bool totalsReady{false}, done{false}, success{false}, removingSource{false};

};

bool operationError(OperationState& state, const std::string& action,

                    const std::string& path, DWORD error) {

  std::lock_guard<std::mutex> lock(state.mutex);

  state.current = path;

  std::string reason;

  switch (error) {

    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: reason = i18n::c("Destination already exists"); break;

    case ERROR_DISK_FULL: reason = i18n::c("Destination is full"); break;

    case ERROR_ACCESS_DENIED: reason = i18n::c("Access denied"); break;

    case ERROR_SHARING_VIOLATION: reason = i18n::c("Item is in use"); break;

    case ERROR_PATH_NOT_FOUND: case ERROR_FILE_NOT_FOUND: reason = i18n::c("Item or drive unavailable"); break;

    case ERROR_NOT_ENOUGH_MEMORY: reason = i18n::c("Not enough memory"); break;

    default: reason = i18n::c("Storage operation failed"); break;

  }

  state.error = action + ": " + reason + i18n::c(" (error ") + std::to_string(error) + ").";

  return false;

}

// Keep rename failures from leaking the source handle in the SDK helper.

BOOL renameFileManagerPath(const std::string& source, const std::string& destination) {

  std::string sourcePath = source;

  while (sourcePath.size() > 3 && sourcePath.back() == char(92)) sourcePath.pop_back();

  ANSI_STRING path;

  RtlInitAnsiString(&path, sourcePath.c_str());

  OBJECT_ATTRIBUTES attributes;

  InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE, ObDosDevicesDirectory(), nullptr);

  IO_STATUS_BLOCK io = {};

  HANDLE handle;

  NTSTATUS status = NtOpenFile(&handle, DELETE | SYNCHRONIZE, &attributes, &io,

      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,

      FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);

  if (!NT_SUCCESS(status)) { SetLastError(RtlNtStatusToDosError(status)); return FALSE; }

  FILE_RENAME_INFORMATION rename = {};

  rename.ReplaceIfExists = FALSE;

  rename.RootDirectory = ObDosDevicesDirectory();

  RtlInitAnsiString(&rename.FileName, destination.c_str());

  status = NtSetInformationFile(handle, &io, &rename, sizeof(rename), FileRenameInformation);

  NTSTATUS closeStatus = NtClose(handle);

  if (NT_SUCCESS(status)) status = closeStatus;

  if (!NT_SUCCESS(status)) { SetLastError(RtlNtStatusToDosError(status)); return FALSE; }

  return TRUE;

}

void operationCurrent(OperationState& state, const std::string& path) {

  std::lock_guard<std::mutex> lock(state.mutex);

  state.current = path;

}

std::string childPath(const std::string& parent, const char* name) {

  return parent + (parent.back() == '\\' ? "" : "\\") + name;

}

// Enumerate incrementally: no in-memory list of an entire game is required.

bool visitChildren(const std::string& path, OperationState& state,

                   const std::function<bool(const std::string&)>& visit) {

  WIN32_FIND_DATAA entry = {};

  HANDLE handle = FindFirstFileA(childPath(path, "*").c_str(), &entry);

  if (handle == INVALID_HANDLE_VALUE) {

    DWORD error = GetLastError();

    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES) return true;

    return operationError(state, i18n::c("Read folder"), path, error);

  }

  bool okay = true;

  DWORD error = ERROR_NO_MORE_FILES;

  do {

    if (strcmp(entry.cFileName, ".") && strcmp(entry.cFileName, "..") &&

        !visit(childPath(path, entry.cFileName))) { okay = false; break; }

    if (!FindNextFileA(handle, &entry)) { error = GetLastError(); break; }

  } while (true);

  FindClose(handle);

  if (okay && error != ERROR_NO_MORE_FILES)

    return operationError(state, i18n::c("Read folder"), path, error);

  return okay;

}

bool countPath(const std::string& path, OperationState& state) {

  operationCurrent(state, path);

  WIN32_FILE_ATTRIBUTE_DATA attributes = {};

  if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attributes))

    return operationError(state, i18n::c("Read item"), path, GetLastError());

  const bool directory = attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;

  {

    std::lock_guard<std::mutex> lock(state.mutex);

    if (directory) ++state.folders;

    else {

      ++state.files;

      state.totalBytes += (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32) |

                          attributes.nFileSizeLow;

    }

  }

  return !directory || visitChildren(path, state,

      [&state](const std::string& child) { return countPath(child, state); });

}

void itemCompleted(OperationState& state, bool file) {

  std::lock_guard<std::mutex> lock(state.mutex);

  ++state.workDone;

  if (file && !state.removingSource) ++state.filesDone;

}

bool copyPath(const std::string& source, const std::string& destination,

              OperationState& state) {

  operationCurrent(state, source);

  DWORD attributes = GetFileAttributesA(source.c_str());

  if (attributes == INVALID_FILE_ATTRIBUTES)

    return operationError(state, i18n::c("Read source"), source, GetLastError());

  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {

    if (!CreateDirectoryA(destination.c_str(), nullptr))

      return operationError(state, i18n::c("Create destination folder"), destination, GetLastError());

    if (!visitChildren(source, state, [&](const std::string& child) {

          return copyPath(child, childPath(destination,

              child.substr(child.find_last_of('\\') + 1).c_str()), state);

        })) return false;

    operationCurrent(state, source);

    itemCompleted(state, false);

    return true;

  }

  HANDLE input = CreateFileA(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,

                             OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

  if (input == INVALID_HANDLE_VALUE)

    return operationError(state, i18n::c("Open source"), source, GetLastError());

  FILETIME lastWrite = {};

  if (!GetFileTime(input, nullptr, nullptr, &lastWrite)) {

    DWORD error = GetLastError(); CloseHandle(input);

    return operationError(state, i18n::c("Read source timestamp"), source, error);

  }

  HANDLE output = CreateFileA(destination.c_str(), GENERIC_WRITE, 0, nullptr,

                              CREATE_NEW, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

  if (output == INVALID_HANDLE_VALUE) {

    DWORD error = GetLastError(); CloseHandle(input);

    return operationError(state, i18n::c("Create destination file"), destination, error);

  }

  auto& buffer = state.copyBuffer;

  bool okay = true;

  for (;;) {

    DWORD received = 0;

    if (!ReadFile(input, buffer, 256 * 1024, &received, nullptr)) {

      okay = operationError(state, i18n::c("Read source"), source, GetLastError()); break;

    }

    if (!received) break;

    DWORD offset = 0;

    while (offset < received) {

      DWORD saved = 0;

      if (!WriteFile(output, buffer + offset, received - offset, &saved, nullptr) || !saved) {

        okay = operationError(state, i18n::c("Write destination"), destination, GetLastError()); break;

      }

      offset += saved;

      std::lock_guard<std::mutex> lock(state.mutex);

      state.bytesDone += saved;

    }

    if (!okay) break;

  }

  if (okay && !SetFileTime(output, nullptr, nullptr, &lastWrite))

    okay = operationError(state, i18n::c("Save destination timestamp"), destination, GetLastError());

  // Report completion only after closing the actual destination successfully.

  if (!CloseHandle(output) && okay)

    okay = operationError(state, i18n::c("Close destination"), destination, GetLastError());

  if (!CloseHandle(input) && okay)

    okay = operationError(state, i18n::c("Close source"), source, GetLastError());

  if (okay && !SetFileAttributesA(destination.c_str(), attributes))

    okay = operationError(state, i18n::c("Save destination attributes"), destination, GetLastError());

  if (okay) itemCompleted(state, true);

  return okay;

}

bool deletePath(const std::string& path, OperationState& state) {

  operationCurrent(state, path);

  DWORD attributes = GetFileAttributesA(path.c_str());

  if (attributes == INVALID_FILE_ATTRIBUTES)

    return operationError(state, i18n::c("Read item"), path, GetLastError());

  bool directory = attributes & FILE_ATTRIBUTE_DIRECTORY;

  if (directory && !visitChildren(path, state,

      [&state](const std::string& child) { return deletePath(child, state); })) return false;

  operationCurrent(state, path);

  if (!SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL))

    return operationError(state, i18n::c("Change item attributes"), path, GetLastError());

  if (!(directory ? RemoveDirectoryA(path.c_str()) : DeleteFileA(path.c_str())))

    return operationError(state, directory ? i18n::c("Delete folder") : i18n::c("Delete file"), path, GetLastError());

  itemCompleted(state, !directory);

  return true;

}

std::string leafName(std::string path);

std::string normalizedPath(std::string path);

bool validDestination(const std::string& source, const std::string& target);

std::string fitFileLabel(Font& font, std::string text, float width, float scale) {

  if (font.measureMenuText(text, 65535, scale).first <= width) return text;

  while (!text.empty()) {

    size_t cut = text.size() - 1;

    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;

    text.resize(cut);

    if (font.measureMenuText(text + "...", 65535, scale).first <= width) break;

  }

  return text + "...";

}

// A bounded panel in the existing theme. The underlying action list is not drawn.

struct FileDialogLayout {

  Renderer& renderer;

  Font& font;

  float x, y, width, bottom;

  FileDialogLayout(Renderer& renderer, Font& font, const std::string& title)

      : renderer(renderer), font(font) {

    x = renderer.getWidth() * 0.45f;

    y = std::max(float(renderer.getSafeTop()), renderer.getHeight() * 0.14f);

    width = renderer.getSafeRight() - x - 16;

    bottom = renderer.getSafeBottom() - 18;

    SDL_Rect panel = {int(x - 12), int(y - 12), int(width + 24), int(bottom - y + 24)};

    renderer.setDrawColor(2, 11, 24, 240);

    renderer.fillRectangle(panel);

    renderer.setDrawColor(0, 174, 235, 170);

    SDL_RenderDrawRect(renderer.getRenderer(), &panel);

    line(title, 1.10f);

    y += 10;

  }

  void line(const std::string& text, float scale = 0.80f, bool selected = false) {

    auto size = font.drawMenuText(fitFileLabel(font, text, width, scale),

        {x, y}, width, scale, !selected, selected);

    if (selected) {

      SDL_Rect box = {int(x - 5), int(y), int(width + 10), int(size.second)};

      renderer.setDrawColor(0, 175, 255, 55); renderer.fillRectangle(box);

      renderer.setDrawColor(0, 215, 255, 255);

      SDL_RenderDrawRect(renderer.getRenderer(), &box);

    }

    y += size.second + 5;

  }

  void paragraph(const std::string& text) {

    // Clip only within this reserved description region, never over other rows.

    SDL_Rect clip = {int(x - 2), int(y - 1), int(width + 4), int(std::max(0.0f, bottom - y))};

    SDL_RenderSetClipRect(renderer.getRenderer(), &clip);

    auto size = font.drawMenuText(text, {x, y}, width, 0.80f, true);

    SDL_RenderSetClipRect(renderer.getRenderer(), nullptr);

    y += size.second + 5;

  }

};

class FileOperationDialog : public SubApp {

  Menu* menu;

  FileExplorerDirectory* listing;

  FileOperation operation;

  std::string source, destination;

  OperationState state;

  HANDLE worker{nullptr};

  static DWORD WINAPI threadMain(void* context) {

    static_cast<FileOperationDialog*>(context)->run(); return 0;

  }

  void joinWorker() {

    if (worker) { WaitForSingleObject(worker, INFINITE); CloseHandle(worker); worker = nullptr; }

  }

  bool renameOnly{false};

  void run() {

    bool okay = false;

    {

      if (operation != FileOperation::Delete && !validDestination(source, destination)) {

        operationError(state, i18n::c("Destination is the source or inside it"), destination, ERROR_INVALID_PARAMETER);

      } else if (countPath(source, state)) {

        {

          std::lock_guard<std::mutex> lock(state.mutex);

          state.totalWork = state.files + state.folders;

          if (operation == FileOperation::Move) state.totalWork *= 2;

          state.totalsReady = true;

          state.phase = operation == FileOperation::Delete ? i18n::c("Deleting contents...") : i18n::c("Transferring contents...");

        }

        if (operation == FileOperation::Delete) okay = deletePath(source, state);

        else if (operation == FileOperation::Move &&

                 normalizedPath(source)[0] == normalizedPath(destination)[0]) {

          if (renameFileManagerPath(source, destination)) {

            std::lock_guard<std::mutex> lock(state.mutex);

            renameOnly = true;

            state.filesDone = state.files;

            state.workDone = state.totalWork;

            state.phase = i18n::c("Moved on the same partition by rename.");

            okay = true;

          } else operationError(state, "Rename/move", destination, GetLastError());

        } else {

          state.copyBuffer = static_cast<char*>(malloc(256 * 1024));

          if (!state.copyBuffer) {

            operationError(state, i18n::c("Allocate copy buffer"), source, ERROR_NOT_ENOUGH_MEMORY);

          }

          okay = state.copyBuffer && copyPath(source, destination, state);

          if (okay) {

            bool unchanged;

            {

              std::lock_guard<std::mutex> lock(state.mutex);

              unchanged = state.filesDone == state.files && state.bytesDone == state.totalBytes &&

                          state.workDone == state.files + state.folders;

            }

            if (!unchanged) okay = operationError(state, i18n::c("Source contents changed during copy"), source, ERROR_INVALID_DATA);

          }

          if (okay && operation == FileOperation::Move) {

            {

              std::lock_guard<std::mutex> lock(state.mutex);

              state.phase = i18n::c("Copy complete; removing source...");

              state.removingSource = true;

            }

            okay = deletePath(source, state);

          }

        }

      }

    }

    std::lock_guard<std::mutex> lock(state.mutex);

    if (okay && state.workDone != state.totalWork) {

      okay = false; state.error = i18n::c("Contents changed during the operation.");

    }

    state.success = okay;

    if (!okay && state.error.empty()) state.error = i18n::c("Operation failed.");

    state.done = true;

  }

  void close() {

    {

      std::lock_guard<std::mutex> lock(state.mutex);

      if (!state.done) return;

      if (state.success && clipboardIsMove && normalizedPath(clipboardPath) == normalizedPath(source))

        clipboardPath.clear();

    }

    joinWorker();

    if (listing) listing->reloadOnNextOpen();

    // Return to the real directory, discarding stale confirmation/action views.

    if (listing) listing->execute(menu);

    SubAppRouter::getInstance()->pop();

  }

public:

  FileOperationDialog(Menu* menu, FileExplorerDirectory* listing, FileOperation operation,

                       const std::string& source, const std::string& destination)

      : menu(menu), listing(listing), operation(operation), source(source), destination(destination) {

    worker = CreateThread(nullptr, 0, threadMain, this, 0, nullptr);

    if (!worker) { state.error = i18n::c("Could not start file operation."); state.done = true; }

  }

  ~FileOperationDialog() override { joinWorker(); }

  void onAPressed() override { close(); }

  void onBPressed() override { close(); }

  void onBackPressed() override { close(); }

  void render(Font& font) override {

    std::string current, phase, error;

    unsigned long long files, filesDone, bytes, totalBytes, work, totalWork;

    bool ready, done, success, renamed;

    {

      std::lock_guard<std::mutex> lock(state.mutex);

      current=state.current; phase=state.phase; error=state.error;

      files=state.files; filesDone=state.filesDone; bytes=state.bytesDone;

      totalBytes=state.totalBytes; work=state.workDone; totalWork=state.totalWork;

      ready=state.totalsReady; done=state.done; success=state.success; renamed=renameOnly;

    }

    auto& renderer = menu->getRenderer();

    FileDialogLayout panel(renderer, font, operation == FileOperation::Delete ? i18n::c("Deleting") :

        operation == FileOperation::Move ? i18n::c("Moving") : i18n::c("Copying"));

    panel.line(leafName(current.empty() ? source : current));

    panel.line(current.empty() ? source : current);

    panel.line(i18n::text(phase));

    const float barY = panel.y + 3;

    SDL_Rect bar = {int(panel.x), int(barY), int(panel.width), 14};

    renderer.setDrawColor(0, 174, 235, 170);

    SDL_RenderDrawRect(renderer.getRenderer(), &bar);

    double fraction = totalWork ? double(work) / totalWork : 0;

    if (operation != FileOperation::Delete && !renamed && totalBytes) {

      // Include a partially copied large file without counting it completed.

      fraction = operation == FileOperation::Move && phase == i18n::c("Copy complete; removing source...")

          ? fraction : double(bytes) / totalBytes * (operation == FileOperation::Move ? 0.5 : 1.0);

    }

    fraction = done && success ? 1.0 : std::min(0.99, fraction);

    SDL_Rect fill = {bar.x+2, bar.y+2, int((bar.w-4)*fraction), bar.h-4};

    if (!ready && !done) {

      fill.w = std::max(1, (bar.w-4)/5);

      fill.x += (SDL_GetTicks()/12) % std::max(1, bar.w-4-fill.w);

    }

    renderer.setDrawColor(0, 175, 255, 180);

    renderer.fillRectangle(fill);

    panel.y = barY + 22;

    panel.line(ready ? std::to_string(int(fraction*100)) + "%" :

        (done ? i18n::c("Totals unavailable") : i18n::c("Calculating totals...")));

    panel.line(i18n::c("Files: ") + std::to_string(filesDone) + " / " +

        (ready ? std::to_string(files) : std::string(i18n::c("counting..."))));

    if (operation != FileOperation::Delete) {

      panel.line(renamed ? i18n::c("No file data copied (same-partition rename).") :

          i18n::c("Bytes: ") + std::to_string(bytes));

      if (!renamed) panel.line(i18n::c("Total bytes: ") + (ready ? std::to_string(totalBytes) : i18n::c("counting...")));

    }

    if (done) {

      panel.line(success ? (operation == FileOperation::Delete ? i18n::c("Delete completed successfully.") :

          operation == FileOperation::Move ? i18n::c("Move completed successfully.") : i18n::c("Copy completed successfully.")) :

          i18n::c("Operation failed."));

      const float footerY = panel.bottom - font.measureMenuText(i18n::c("A / B: return to folder"), panel.width, 0.80f).second;

      panel.bottom = footerY - 8; // Keep wrapped errors clear of the footer.

      if (!success) panel.paragraph(error +

          (operation == FileOperation::Move ? i18n::c(" Original items may remain; check both locations.") :

           operation == FileOperation::Copy ? i18n::c(" Partial destination items may remain.") : i18n::c(" Some items may already be deleted.")));

      // Fixed footer is reserved separately from wrapped failure descriptions.

      font.drawMenuText(i18n::c("A / B: return to folder"), {panel.x, footerY}, panel.width, 0.80f, true);

    } else panel.line(i18n::c("Working; return when finished."));

  }

};

void startFileOperation(Menu* menu, FileExplorerDirectory* listing, FileOperation operation,

                        const std::string& source, const std::string& destination = "") {

  SubAppRouter::getInstance()->push(std::make_shared<FileOperationDialog>(

      menu, listing, operation, source, destination));

}

class FileDeleteConfirmation : public SubApp {

  Menu* menu;

  FileExplorerDirectory* listing;

  std::string path;

  bool confirm{false};

public:

  FileDeleteConfirmation(Menu* menu, FileExplorerDirectory* listing, const std::string& path)

      : menu(menu), listing(listing), path(path) {}

  void onUpPressed() override { confirm = !confirm; }

  void onDownPressed() override { confirm = !confirm; }

  void onBPressed() override { SubAppRouter::getInstance()->pop(); }

  void onBackPressed() override { onBPressed(); }

  void onAPressed() override {

    Menu* owner = menu; auto* directory = listing; std::string item = path; bool erase = confirm;

    SubAppRouter::getInstance()->pop();

    if (erase) startFileOperation(owner, directory, FileOperation::Delete, item);

  }

  void render(Font& font) override {

    FileDialogLayout panel(menu->getRenderer(), font, i18n::c("Confirm delete"));

    panel.line(leafName(path));

    panel.line(path);

    panel.y += 8;

    panel.line(i18n::c("No, keep it"), 0.88f, !confirm);

    panel.line(i18n::c("Yes, permanently delete"), 0.88f, confirm);

    panel.y += 10;

    panel.paragraph(i18n::c("Permanently deletes this item and all of its contents. This cannot be undone. B cancels."));

  }

};

class FileResultDialog : public SubApp {

  Menu* menu;

  std::string message;

public:

  FileResultDialog(Menu* menu, const std::string& message) : menu(menu), message(message) {}

  void onAPressed() override { SubAppRouter::getInstance()->pop(); }

  void onBPressed() override { onAPressed(); }

  void onBackPressed() override { onAPressed(); }

  void render(Font& font) override {

    FileDialogLayout panel(menu->getRenderer(),font,i18n::c("File manager"));

    panel.paragraph(message);

    font.drawMenuText(i18n::c("A / B: return"), {panel.x,panel.bottom+3},panel.width,0.80f,true);

  }

};

// Only file-explorer nodes opt into this renderer; other dashboard menus stay unchanged.

bool renderExplorerList(MenuNode& node, Font& font, Renderer& renderer) {

  const float x = std::max(float(renderer.getSafeLeft()), renderer.getWidth()*0.63f);

  const float width = renderer.getSafeRight() - x - 12;

  float y = std::max(float(renderer.getSafeTop()), renderer.getHeight()*0.1f);

  auto title = font.drawMenuText(fitFileLabel(font, std::string(node.getLabel()), width, 1.10f),

      {x, y}, width, 1.10f, true);

  y += title.second + 12; // Actual rendered title height, never a fixed one-line offset.

  auto& children = *node.getChildNodes();

  std::string footer;

  std::vector<size_t> rows;

  for (size_t i=0; i<children.size(); ++i) {

    if (children[i]->isSelectable()) rows.push_back(i);

    else {

      if (!footer.empty()) footer += "\n";

      std::string description(children[i]->getLabel());

      footer += description.compare(0,13,i18n::c("Destination: ")) == 0

          ? fitFileLabel(font,description,width,0.80f) : description;

    }

  }

  const float bottom = renderer.getSafeBottom() - 6;

  const float rowHeight = font.measureMenuText("Ag", width, 0.88f).second + 5;

  float footerHeight = footer.empty() ? 0 : font.measureMenuText(footer, width, 0.80f).second + 12;

  footerHeight = std::min(footerHeight, std::max(0.0f, bottom-y-rowHeight));

  const float listBottom = bottom-footerHeight;

  const size_t visible = std::max(1, int((listBottom-y)/rowHeight));

  size_t selectedRow = 0;

  for (size_t i=0;i<rows.size();++i) if(rows[i]==node.getSelected()) selectedRow=i;

  const size_t first = selectedRow >= visible ? selectedRow-visible+1 : 0;

  for (size_t i=first; i<rows.size() && i<first+visible; ++i) {

    auto& item=children[rows[i]];

    auto size=font.drawMenuText(fitFileLabel(font, std::string(item->getLabel()), width, 0.88f),

        {x,y}, width, 0.88f, false, rows[i]==node.getSelected());

    if (rows[i]==node.getSelected()) {

      SDL_Rect box={int(x-5),int(y),int(width+10),int(size.second)};

      renderer.setDrawColor(0,175,255,55); renderer.fillRectangle(box);

      renderer.setDrawColor(0,215,255,255); SDL_RenderDrawRect(renderer.getRenderer(),&box);

    }

    y += std::max(rowHeight,size.second+5);

  }

  if (!footer.empty()) {

    const float footerY = std::max(y+7,bottom-footerHeight+7);

    SDL_Rect clip={int(x-2),int(footerY-1),int(width+4),int(std::max(0.0f,bottom-footerY))};

    SDL_RenderSetClipRect(renderer.getRenderer(),&clip);

    font.drawMenuText(footer,{x,footerY},width,0.80f,true);

    SDL_RenderSetClipRect(renderer.getRenderer(),nullptr);

  }

  return true;

}

class ExplorerMenuNode : public MenuNode {

public:

  bool translateLabel;
  ExplorerMenuNode(MenuNode* parent, const std::string& label, bool translateLabel=true) : MenuNode(parent,label), translateLabel(translateLabel) {}
  std::string_view getLabel() const override {return translateLabel ? MenuItem::getLabel() : std::string_view(label);}

  bool renderCustomMenu(Font& font, Renderer& renderer) override {

    return renderExplorerList(*this,font,renderer);

  }

};

std::string leafName(std::string path) {

  while (path.size() > 3 && (path.back() == '\\' || path.back() == '/')) path.pop_back();

  size_t slash = path.find_last_of("\\/");

  return slash == std::string::npos ? path : path.substr(slash + 1);

}

bool isDriveRoot(const std::string& path) {

  return path.size() == 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/');

}

std::string normalizedPath(std::string path) {

  std::replace(path.begin(), path.end(), '/', '\\');

  while (path.size() > 3 && path.back() == '\\') path.pop_back();

  std::transform(path.begin(), path.end(), path.begin(),

                 [](unsigned char c) { return static_cast<char>(tolower(c)); });

  return path;

}

bool validDestination(const std::string& source, const std::string& target) {

  const std::string from = normalizedPath(source);

  const std::string to = normalizedPath(target);

  // Reject self-copy and copying a folder into its own subtree.

  return from != to && to.compare(0, from.size() + 1, from + "\\") != 0;

}

void showResult(Menu* menu, MenuNode* owner, FileExplorerDirectory* directory,

                const std::string& message);

class FileExplorerCallbackAction : public MenuItem {

public:

  FileExplorerCallbackAction(MenuNode* owner, const std::string& label,

                             std::function<void(Menu*)> callback)

      : MenuItem(owner, label), callback(std::move(callback)) {}

  void execute(Menu* menu) override { callback(menu); }

private:

  std::function<void(Menu*)> callback;

};

class FileTreeAction : public MenuItem {

public:

  FileTreeAction(MenuNode* owner, FileExplorerDirectory* listing,

                 const std::string& label, const std::string& source,

                 const std::string& destination, bool move)

      : MenuItem(owner, label), listing(listing), source(source),

        destination(destination), move(move) {}

  void execute(Menu* menu) override {

    startFileOperation(menu, listing, move ? FileOperation::Move : FileOperation::Copy, source, destination);

  }

private:

  FileExplorerDirectory* listing;

  std::string source, destination;

  bool move;

};

class ClipboardAction : public MenuItem {

public:

  ClipboardAction(MenuNode* owner, FileExplorerDirectory* listing,

                  const std::string& source, bool move)

      : MenuItem(owner, move ? i18n::c("Move to clipboard") : i18n::c("Copy to clipboard")), listing(listing),

        source(source), move(move) {}

  void execute(Menu* menu) override {

    clipboardPath = source;

    clipboardIsMove = move;

    showResult(menu, parentNode, listing,

               move ? i18n::c("Selected item ready to move. Choose Paste in a destination folder.")

                    : i18n::c("Selected item copied. Choose Paste in a destination folder."));

  }

private:

  FileExplorerDirectory* listing;

  std::string source;

  bool move;

};

class PasteAction : public MenuItem {

public:

  PasteAction(MenuNode* owner, FileExplorerDirectory* listing,

              const std::string& destination)

      : MenuItem(owner, i18n::c("Paste here")), listing(listing), destination(destination) {}

  void execute(Menu* menu) override {

    if (clipboardPath.empty()) {

      showResult(menu, parentNode, listing, i18n::c("Nothing has been copied or moved yet."));

      return;

    }

    std::string target = childPath(destination, leafName(clipboardPath).c_str());

    startFileOperation(menu, listing, clipboardIsMove ? FileOperation::Move : FileOperation::Copy,

                       clipboardPath, target);

  }

private:

  FileExplorerDirectory* listing;

  std::string destination;

};

void showResult(Menu* menu, MenuNode*, FileExplorerDirectory*, const std::string& message) {

  SubAppRouter::getInstance()->push(std::make_shared<FileResultDialog>(menu,message));

}

class FileCopyAction : public MenuItem {

public:

  FileCopyAction(MenuNode* owner, FileExplorerDirectory* directory,

                 const std::string& label, const std::string& source,

                 const std::string& destination)

      : MenuItem(owner, label), directory(directory), source(source),

        destination(destination) {}

  void execute(Menu* menu) override {

    startFileOperation(menu, directory, FileOperation::Copy, source, destination);

  }

private:

  FileExplorerDirectory* directory;

  std::string source;

  std::string destination;

};

class FileMoveAction : public MenuItem {

public:

  FileMoveAction(MenuNode* owner, FileExplorerDirectory* directory,

                 const std::string& label, const std::string& source,

                 const std::string& destination)

      : MenuItem(owner, label), directory(directory), source(source),

        destination(destination) {}

  void execute(Menu* menu) override {

    startFileOperation(menu, directory, FileOperation::Move, source, destination);

  }

private:

  FileExplorerDirectory* directory;

  std::string source;

  std::string destination;

};

class DestinationPicker : public ExplorerMenuNode {

public:

  DestinationPicker(MenuNode* owner, FileExplorerDirectory* listing,

                     const std::string& source, bool move,

                     const std::string& folder = "", const std::string& label = "")

      : ExplorerMenuNode(owner, label.empty() ? (move ? i18n::c("Move") : i18n::c("Copy")) : label),

        listing(listing), source(source), folder(folder), move(move) {}

  void execute(Menu* menu) override {

    if (!loaded) { load(); loaded = true; }

    MenuNode::execute(menu);

  }

private:

  void load() {

    if (folder.empty()) {

      for (char drive : std::string("CEFGXYZ")) {

        const std::string root = std::string(1, drive) + ":\\";

        if (GetFileAttributesA(root.c_str()) != INVALID_FILE_ATTRIBUTES) {

          addNode(std::make_shared<DestinationPicker>(this, listing, source,

                                                      move, root, root));

        }

      }

      addNode(std::make_shared<ClipboardAction>(this, listing, source, move));

      addNode(std::make_shared<MenuNonInteractive>(this,

          i18n::c("Choose a drive, browse folders, then select Copy here or Move here. B goes back.")));

      return;

    }

    const std::string target = folder + leafName(source);

    if (validDestination(source, target)) {

      addNode(std::make_shared<FileTreeAction>(this, listing,

          move ? i18n::c("Move here") : i18n::c("Copy here"), source, target, move));

    }

    WIN32_FIND_DATAA entry = {};

    HANDLE search = FindFirstFileA((folder + "*").c_str(), &entry);

    std::vector<std::string> names;

    if (search != INVALID_HANDLE_VALUE) {

      do {

        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&

            strcmp(entry.cFileName, ".") && strcmp(entry.cFileName, "..")) {

          const std::string child = folder + entry.cFileName + "\\";

          const std::string from = normalizedPath(source);

          const std::string to = normalizedPath(child);

          if (from != to && to.compare(0, from.size() + 1, from + "\\") != 0)

            names.push_back(entry.cFileName);

        }

      } while (FindNextFileA(search, &entry));

      FindClose(search);

    }

    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {

      return _stricmp(a.c_str(), b.c_str()) < 0;

    });

    for (const auto& name : names) {

      addNode(std::make_shared<DestinationPicker>(this, listing, source, move,

                                                  folder + name + "\\", name));

    }

    addNode(std::make_shared<MenuNonInteractive>(this, i18n::c("Destination: ") + folder));

  }

  FileExplorerDirectory* listing;

  std::string source, folder;

  bool move;

  bool loaded{ false };

};

class FileDeleteAction : public MenuItem {

public:

  FileDeleteAction(MenuNode* owner, FileExplorerDirectory* directory, const std::string& path)

      : MenuItem(owner,i18n::c("Delete...")), directory(directory), path(path) {}

  void execute(Menu* menu) override {

    SubAppRouter::getInstance()->push(std::make_shared<FileDeleteConfirmation>(menu,directory,path));

  }

private:

  FileExplorerDirectory* directory;

  std::string path;

};

class FileLaunchAction : public MenuItem {

public:

  FileLaunchAction(MenuNode* parent, const std::string& path)

      : MenuItem(parent, i18n::c("Launch this XBE")), path(path) {}

  void execute(Menu*) override {

    std::string launchPath = path;

    if (launchPath.size() > 1 && launchPath[0] == 'D' && launchPath[1] == ':') {

      launchPath.replace(0, 2, "\\Device\\CdRom0");

    }

    XBELauncher::launch(launchPath);

  }

private:

  std::string path;

};

#endif

}

FileExplorerDirectory::FileExplorerDirectory(MenuNode* parent,

                                             const std::string& label,

                                             const std::string& path)

    : MenuNode(parent, label), path(path) {}

void FileExplorerDirectory::reloadOnNextOpen() {

  loaded = false;

}

void FileExplorerDirectory::executeStart(Menu* menu) {

#ifdef NXDK

  if (path.empty()) { execute(menu); return; }

  actionMenu = std::make_shared<ExplorerMenuNode>(parentNode, i18n::c("Actions - ") + leafName(path));

  auto listing = static_cast<FileExplorerDirectory*>(parentNode);

  actionMenu->addNode(std::make_shared<FileExplorerCallbackAction>(actionMenu.get(),

      i18n::c("Open folder"), [this](Menu* active) { execute(active); }));

  const bool disc = toupper(path[0]) == 'D';

  if (!isDriveRoot(path)) {

    actionMenu->addNode(std::make_shared<DestinationPicker>(actionMenu.get(), listing,

                                                          path, false));

    if (!disc) {

      actionMenu->addNode(std::make_shared<DestinationPicker>(actionMenu.get(), listing,

                                                            path, true));

      actionMenu->addNode(std::make_shared<FileDeleteAction>(actionMenu.get(), listing, path));

    }

  }

  if (!disc) actionMenu->addNode(std::make_shared<PasteAction>(actionMenu.get(), listing, path));

  actionMenu->addNode(std::make_shared<MenuNonInteractive>(actionMenu.get(),

      i18n::c("Copy and Move let you browse destination folders. Delete asks for confirmation.")));

  menu->setCurrentMenu(actionMenu.get());

#else

  execute(menu);

#endif

}

void FileExplorerDirectory::execute(Menu* menu) {

  if (!loaded) {

    childNodes.clear();

    loadChildren();

    loaded = true;

  }

  MenuNode::execute(menu);

}

void FileExplorerDirectory::loadChildren() {

#ifdef NXDK

  if (path.empty()) {

    const char* drives[] = { "C", "E", "F", "G", "X", "Y", "Z", "D" };

    for (const char* drive : drives) {

      std::string root = std::string(drive) + ":\\";

      if (GetFileAttributesA(root.c_str()) != INVALID_FILE_ATTRIBUTES) {

        addNode(std::make_shared<FileExplorerDirectory>(this, root, root));

      }

    }

    return;

  }

  std::string search = path + "*";

  WIN32_FIND_DATAA entry = {};

  HANDLE handle = FindFirstFileA(search.c_str(), &entry);

  if (handle == INVALID_HANDLE_VALUE) {

    addNode(std::make_shared<MenuNonInteractive>(this, i18n::c("This folder could not be read.")));

    return;

  }

  std::vector<WIN32_FIND_DATAA> entries;

  do {

    if (strcmp(entry.cFileName, ".") != 0 && strcmp(entry.cFileName, "..") != 0) {

      entries.push_back(entry);

    }

  } while (FindNextFileA(handle, &entry));

  FindClose(handle);

  std::sort(entries.begin(), entries.end(), [](const WIN32_FIND_DATAA& a,

                                                const WIN32_FIND_DATAA& b) {

    bool aDirectory = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    bool bDirectory = (b.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (aDirectory != bDirectory) return aDirectory;

    return _stricmp(a.cFileName, b.cFileName) < 0;

  });

  for (const WIN32_FIND_DATAA& item : entries) {

    std::string childPath = path + item.cFileName;

    if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

      childPath += "\\";

      addNode(std::make_shared<FileExplorerDirectory>(

          this, std::string("[DIR] ") + item.cFileName, childPath));

      continue;

    }

    auto file = std::make_shared<ExplorerMenuNode>(this, item.cFileName, false);

    std::string lowerName = item.cFileName;

    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    if (lowerName.size() >= 4 && lowerName.substr(lowerName.size() - 4) == ".xbe") {

      file->addNode(std::make_shared<FileLaunchAction>(file.get(), childPath));

    }

    const bool readOnlyDisc = !path.empty() && toupper(path[0]) == 'D';

    file->addNode(std::make_shared<DestinationPicker>(file.get(), this, childPath, false));

    if (!readOnlyDisc) {

      file->addNode(std::make_shared<DestinationPicker>(file.get(), this, childPath, true));

      file->addNode(std::make_shared<FileDeleteAction>(file.get(), this, childPath));

      file->addNode(std::make_shared<PasteAction>(file.get(), this, path));

    }    addNode(file);

  }

  if (entries.empty()) {

    addNode(std::make_shared<MenuNonInteractive>(this, i18n::c("This folder is empty.")));

  }

#endif

}

bool FileExplorerDirectory::renderCustomMenu(Font& font, Renderer& renderer) {

#ifdef NXDK

  return renderExplorerList(*this,font,renderer);

#else

  return false;

#endif

}