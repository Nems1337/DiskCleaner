#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <iomanip>
#include <thread>
#include <chrono>
#include <regex>
#include <future>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <sstream>
#include <io.h>
#include <fcntl.h>
#include <fstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

// =====================================================================================
// VERSION SYSTEM
// =====================================================================================
// Format: MAJOR.MINOR.PATCH[SUFFIX]
// 
// MAJOR: Incremented for major feature additions or breaking changes (e.g., 2.x.x -> 3.x.x)
// MINOR: Incremented for significant new features (e.g., 2.1.x -> 2.2.x)  
// PATCH: Incremented for bug fixes and minor improvements (e.g., 2.1.0 -> 2.1.1)
// SUFFIX: Used for incremental updates within the same patch:
//         "a", "b", "c", etc. for bug fixes and hotfixes
//         "" (empty) for stable releases
//
// Examples:
//   2.1.0    - Stable release with new features
//   2.1.1a   - First hotfix for 2.1.1
//   2.1.1b   - Second hotfix for 2.1.1  
//   2.2.0    - Next minor release with new features
//   3.0.0    - Major version update
// =====================================================================================


#define VERSION_MAJOR 2
#define VERSION_MINOR 4
#define VERSION_PATCH 0
#define VERSION_SUFFIX "c"

#define VERSION_STRING "2.4.0c"
#define APP_TITLE_STRING "DiskCleaner v" VERSION_STRING


#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define ID_LISTVIEW 1001
#define ID_BTN_SELECTALL 1002
#define ID_BTN_DESELECTALL 1003
#define ID_BTN_REFRESH 1004
#define ID_BTN_CLEANUP 1005
#define ID_BTN_RECYCLEBIN 1006
#define ID_CHK_DRYRUN 1007
#define ID_CHK_VERBOSE 1008
#define ID_PROGRESS_OVERALL 1009
#define ID_STATUS 1010
#define ID_RESULTS 1011
#define ID_BTN_DRYRUN_INFO 1012
#define ID_BTN_VERBOSE_INFO 1013
#define ID_MENU_ADD_DIR 1014
#define ID_MENU_REMOVE_DIR 1015

struct CleanupItem {
    std::string name;
    std::string path;
    std::string description;
    bool enabled;
    bool requiresAdmin;
    bool isCustom;
    uintmax_t size;
};

struct CleanupResult {
    std::string itemName;
    uintmax_t bytesRemoved;
    int filesDeleted;
    int filesSkipped;
    bool success;
    std::string errorMessage;
    std::chrono::milliseconds duration;
};

class DiskCleanerGUI {
private:
    HWND hwndMain;
    HWND hwndListView;
    HWND hwndProgressOverall;
    HWND hwndStatus;
    HWND hwndResults;
    HWND hwndBtnSelectAll;
    HWND hwndBtnDeselectAll;
    HWND hwndBtnRefresh;
    HWND hwndBtnCleanup;
    HWND hwndBtnRecycleBin;
    HWND hwndChkDryRun;
    HWND hwndChkVerbose;
    HWND hwndBtnDryRunInfo;
    HWND hwndBtnVerboseInfo;
    
    std::vector<CleanupItem> cleanupItems;
    bool dryRunMode = false;
    bool verboseMode = false;
    std::atomic<int> completedTasks{0};
    std::atomic<int> totalTasks{0};
    std::mutex logMutex;
    bool isCleanupRunning = false;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        DiskCleanerGUI* pThis = nullptr;
        
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            pThis = reinterpret_cast<DiskCleanerGUI*>(pCreate->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->hwndMain = hwnd;  // Set the window handle
        } else {
            pThis = reinterpret_cast<DiskCleanerGUI*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        
        if (pThis) {
            return pThis->HandleMessage(uMsg, wParam, lParam);
        }
        
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_CREATE:
                CreateControls();
                SetupCleanupItems();
                PopulateListView();
                std::thread([this]() { CalculateSizesAsync(); }).detach();
                return 0;
                
            case WM_COMMAND:
                if (lParam == 0) { 
                    // Menu command (lParam is 0 for menu items)
                    HandleMenuCommand(LOWORD(wParam));
                } else {
                    // Control command (lParam contains control handle)
                    HandleCommand(LOWORD(wParam));
                }
                return 0;
                
            case WM_NOTIFY:
                return HandleNotify(reinterpret_cast<LPNMHDR>(lParam));
                
            case WM_SIZE:
                ResizeControls();
                return 0;
                
            case WM_USER + 1:
                PopulateListView();
                return 0;
                
            case WM_CLOSE:
                if (isCleanupRunning) {
                    if (MessageBox(hwndMain, L"Cleanup is running. Are you sure you want to exit?", 
                                 L"Confirm Exit", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                        return 0;
                    }
                }
                DestroyWindow(hwndMain);
                return 0;
                
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
                
            case ID_CHK_VERBOSE:
                verboseMode = (SendMessage(hwndChkVerbose, BM_GETCHECK, 0, 0) == BST_CHECKED);
                break;
                
            case ID_BTN_DRYRUN_INFO:
                MessageBox(hwndMain, 
                    L"Dry Run Mode:\n\n"
                    L"• Simulates the cleanup process without actually deleting files\n"
                    L"• Shows what would be deleted and how much space would be freed\n"
                    L"• Safe to use for testing - no files are permanently removed\n"
                    L"• Useful for previewing cleanup results before actual deletion",
                    L"Dry Run Mode - Information", 
                    MB_OK | MB_ICONINFORMATION);
                break;
                
            case ID_BTN_VERBOSE_INFO:
                MessageBox(hwndMain,
                    L"Verbose Mode:\n\n"
                    L"• Shows detailed information during cleanup operations\n"
                    L"• Displays individual files and folders being processed\n"
                    L"• Reports specific errors and skipped items\n"
                    L"• Provides comprehensive logging in the results area\n"
                    L"• Helpful for troubleshooting and monitoring progress",
                    L"Verbose Mode - Information",
                    MB_OK | MB_ICONINFORMATION);
                break;
                
            default:
                return DefWindowProc(hwndMain, uMsg, wParam, lParam);
        }
        return 0;
    }

    void CreateControls() {
        // Create menu bar
        HMENU hMenuBar = CreateMenu();
        HMENU hFileMenu = CreatePopupMenu();
        
        AppendMenu(hFileMenu, MF_STRING, ID_MENU_ADD_DIR, L"&Add Directory...");
        AppendMenu(hFileMenu, MF_STRING, ID_MENU_REMOVE_DIR, L"&Remove Selected Directory");
        AppendMenu(hFileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(hFileMenu, MF_STRING, SC_CLOSE, L"E&xit");
        
        AppendMenu(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"&File");
        SetMenu(hwndMain, hMenuBar);
        
        hwndListView = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEW,
            L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
            10, 10, 600, 350,
            hwndMain, (HMENU)ID_LISTVIEW, GetModuleHandle(nullptr), nullptr
        );
        
        if (!hwndListView) return;
        
        ListView_SetExtendedListViewStyle(hwndListView, 
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);
        LVCOLUMN lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        
        lvc.pszText = (LPWSTR)L"Item";
        lvc.cx = 200;
        lvc.iSubItem = 0;
        ListView_InsertColumn(hwndListView, 0, &lvc);
        
        lvc.pszText = (LPWSTR)L"Size";
        lvc.cx = 100;
        lvc.iSubItem = 1;
        ListView_InsertColumn(hwndListView, 1, &lvc);
        
        lvc.pszText = (LPWSTR)L"Description";
        lvc.cx = 250;
        lvc.iSubItem = 2;
        ListView_InsertColumn(hwndListView, 2, &lvc);
        
        lvc.pszText = (LPWSTR)L"Path";
        lvc.cx = 200;
        lvc.iSubItem = 3;
        ListView_InsertColumn(hwndListView, 3, &lvc);

        hwndBtnSelectAll = CreateWindow(L"BUTTON", L"Select All",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            620, 10, 100, 30, hwndMain, (HMENU)ID_BTN_SELECTALL, GetModuleHandle(nullptr), nullptr);
            
        hwndBtnDeselectAll = CreateWindow( L"BUTTON", L"Deselect All",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            620, 50, 100, 30, hwndMain, (HMENU)ID_BTN_DESELECTALL, GetModuleHandle(nullptr), nullptr);
            
        hwndBtnRefresh = CreateWindow(L"BUTTON", L"Refresh Sizes",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            620, 90, 100, 30, hwndMain, (HMENU)ID_BTN_REFRESH, GetModuleHandle(nullptr), nullptr);
            
        hwndBtnCleanup = CreateWindow(L"BUTTON", L"Start Cleanup",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            620, 150, 120, 40, hwndMain, (HMENU)ID_BTN_CLEANUP, GetModuleHandle(nullptr), nullptr);
            
        hwndBtnRecycleBin = CreateWindow(L"BUTTON", L"Clean Recycle Bin",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            620, 200, 130, 30, hwndMain, (HMENU)ID_BTN_RECYCLEBIN, GetModuleHandle(nullptr), nullptr);

        hwndChkDryRun = CreateWindow(L"BUTTON", L"Dry Run",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            620, 250, 100, 20, hwndMain, (HMENU)ID_CHK_DRYRUN, GetModuleHandle(nullptr), nullptr);
            
        hwndBtnDryRunInfo = CreateWindow(L"BUTTON", L"?",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            725, 249, 20, 22, hwndMain, (HMENU)ID_BTN_DRYRUN_INFO, GetModuleHandle(nullptr), nullptr);
            
        hwndChkVerbose = CreateWindow(L"BUTTON", L"Verbose",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            620, 280, 100, 20, hwndMain, (HMENU)ID_CHK_VERBOSE, GetModuleHandle(nullptr), nullptr);
            
        hwndBtnVerboseInfo = CreateWindow(L"BUTTON", L"?",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            725, 279, 20, 22, hwndMain, (HMENU)ID_BTN_VERBOSE_INFO, GetModuleHandle(nullptr), nullptr);

        hwndProgressOverall = CreateWindow(PROGRESS_CLASS, nullptr,
            WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
            10, 370, 600, 20, hwndMain, (HMENU)ID_PROGRESS_OVERALL, GetModuleHandle(nullptr), nullptr);
            
        hwndStatus = CreateWindow(L"STATIC", L"Ready",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            10, 400, 600, 20, hwndMain, (HMENU)ID_STATUS, GetModuleHandle(nullptr), nullptr);
            
        hwndResults = CreateWindow(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
            10, 430, 730, 120, hwndMain, (HMENU)ID_RESULTS, GetModuleHandle(nullptr), nullptr);
    }

    void ResizeControls() {
        RECT rect;
        GetClientRect(hwndMain, &rect);
        
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        
        SetWindowPos(hwndListView, nullptr, 10, 10, width - 160, height - 200, SWP_NOZORDER);
        int btnX = width - 140;
        SetWindowPos(hwndBtnSelectAll, nullptr, btnX, 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(hwndBtnDeselectAll, nullptr, btnX, 50, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(hwndBtnRefresh, nullptr, btnX, 90, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(hwndBtnCleanup, nullptr, btnX, 150, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(hwndBtnRecycleBin, nullptr, btnX, 200, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(hwndChkDryRun, nullptr, btnX, 250, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(hwndChkVerbose, nullptr, btnX, 280, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(hwndBtnDryRunInfo, nullptr, btnX + 105, 249, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(hwndBtnVerboseInfo, nullptr, btnX + 105, 279, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        
        int bottomY = height - 180;
        SetWindowPos(hwndProgressOverall, nullptr, 10, bottomY, width - 160, 20, SWP_NOZORDER);
        SetWindowPos(hwndStatus, nullptr, 10, bottomY + 30, width - 160, 20, SWP_NOZORDER);
        SetWindowPos(hwndResults, nullptr, 10, bottomY + 60, width - 20, 100, SWP_NOZORDER);
    }

    void HandleCommand(WORD commandId) {
        switch (commandId) {
            case ID_BTN_SELECTALL:
                SelectAllItems(true);
                break;
                
            case ID_BTN_DESELECTALL:
                SelectAllItems(false);
                break;
                
            case ID_BTN_REFRESH:
                std::thread([this]() { CalculateSizesAsync(); }).detach();
                break;
                
            case ID_BTN_CLEANUP:
                if (!isCleanupRunning) {
                    std::thread([this]() { StartParallelCleanup(); }).detach();
                }
                break;
                
            case ID_BTN_RECYCLEBIN:
                std::thread([this]() { EmptyRecycleBin(); }).detach();
                break;
                
            case ID_CHK_DRYRUN:
                dryRunMode = (SendMessage(hwndChkDryRun, BM_GETCHECK, 0, 0) == BST_CHECKED);
                break;
                
            case ID_CHK_VERBOSE:
                verboseMode = (SendMessage(hwndChkVerbose, BM_GETCHECK, 0, 0) == BST_CHECKED);
                break;
                
            case ID_BTN_DRYRUN_INFO:
                MessageBox(hwndMain, 
                    L"Dry Run Mode:\n\n"
                    L"• Simulates the cleanup process without actually deleting files\n"
                    L"• Shows what would be deleted and how much space would be freed\n"
                    L"• Safe to use for testing - no files are permanently removed\n"
                    L"• Useful for previewing cleanup results before actual deletion",
                    L"Dry Run Mode - Information", 
                    MB_OK | MB_ICONINFORMATION);
                break;
                
            case ID_BTN_VERBOSE_INFO:
                MessageBox(hwndMain,
                    L"Verbose Mode:\n\n"
                    L"• Shows detailed information during cleanup operations\n"
                    L"• Displays individual files and folders being processed\n"
                    L"• Reports specific errors and skipped items\n"
                    L"• Provides comprehensive logging in the results area\n"
                    L"• Helpful for troubleshooting and monitoring progress",
                    L"Verbose Mode - Information",
                    MB_OK | MB_ICONINFORMATION);
                break;
        }
    }

    void HandleMenuCommand(WORD commandId) {
        switch (commandId) {
            case ID_MENU_ADD_DIR:
                AddCustomDirectory();
                break;
                
            case ID_MENU_REMOVE_DIR:
                RemoveSelectedDirectory();
                break;
                
            case SC_CLOSE:
                PostMessage(hwndMain, WM_CLOSE, 0, 0);
                break;
        }
    }

    LRESULT HandleNotify(LPNMHDR pnmh) {
        if (pnmh->idFrom == ID_LISTVIEW && pnmh->code == LVN_ITEMCHANGED) {
            LPNMLISTVIEW pnmlv = reinterpret_cast<LPNMLISTVIEW>(pnmh);
            if ((pnmlv->uChanged & LVIF_STATE) && 
                ((pnmlv->uOldState & LVIS_STATEIMAGEMASK) != (pnmlv->uNewState & LVIS_STATEIMAGEMASK))) {
                
                bool checked = ListView_GetCheckState(hwndListView, pnmlv->iItem);
                if (pnmlv->iItem < static_cast<int>(cleanupItems.size())) {
                    cleanupItems[pnmlv->iItem].enabled = checked;
                    UpdateStatusBar();
                }
            }
        }
        return 0;
    }

    void SelectAllItems(bool select) {
        int itemCount = ListView_GetItemCount(hwndListView);
        for (int i = 0; i < itemCount; ++i) {
            ListView_SetCheckState(hwndListView, i, select);
            if (i < static_cast<int>(cleanupItems.size())) {
                cleanupItems[i].enabled = select;
            }
        }
        UpdateStatusBar();
    }

    bool IsAdmin() {
        BOOL isAdmin = FALSE;
        PSID adminGroup = NULL;
        SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
        
        if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                   DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
            CheckTokenMembership(NULL, adminGroup, &isAdmin);
            FreeSid(adminGroup);
        }
        return isAdmin == TRUE;
    }

    void RelaunchAsAdmin() {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = exePath;
        sei.hwnd = hwndMain;
        sei.nShow = SW_NORMAL;
        
        if (ShellExecuteExW(&sei)) {
            PostMessage(hwndMain, WM_CLOSE, 0, 0);
        } else {
            MessageBox(hwndMain, L"Failed to relaunch with admin privileges.", L"Error", MB_OK | MB_ICONERROR);
        }
    }

    std::string GetEnvironmentVariable(const std::string& name) {
        char* value = nullptr;
        size_t size = 0;
        if (_dupenv_s(&value, &size, name.c_str()) == 0 && value != nullptr) {
            std::string result(value);
            free(value);
            return result;
        }
        return "";
    }

    std::string FormatBytes(uintmax_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unitIndex = 0;
        double size = static_cast<double>(bytes);
        
        while (size >= 1024.0 && unitIndex < 4) {
            size /= 1024.0;
            unitIndex++;
        }
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
        return oss.str();
    }

    std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring result(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
        return result;
    }

    // Version functions
    std::string GetVersionString() {
        return VERSION_STRING;
    }

    std::wstring GetAppTitle() {
        return StringToWString(APP_TITLE_STRING);
    }

    void SetStatusText(const std::string& text) {
        std::wstring wtext = StringToWString(text);
        SetWindowText(hwndStatus, wtext.c_str());
    }

    void AppendToResults(const std::string& text) {
        std::lock_guard<std::mutex> lock(logMutex);
        
        int length = GetWindowTextLength(hwndResults);
        
        SendMessage(hwndResults, EM_SETSEL, length, length);
        std::wstring wtext = StringToWString(text + "\r\n");
        SendMessage(hwndResults, EM_REPLACESEL, FALSE, (LPARAM)wtext.c_str());
        
        SendMessage(hwndResults, EM_SCROLLCARET, 0, 0);
    }

    void UpdateProgress(int current, int total) {
        if (total > 0) {
            int percent = (current * 100) / total;
            SendMessage(hwndProgressOverall, PBM_SETPOS, percent, 0);
            
            std::ostringstream oss;
            oss << "Progress: " << current << "/" << total << " (" << percent << "%)";
            SetStatusText(oss.str());
        }
    }

    void UpdateStatusBar() {
        uintmax_t totalSize = 0;
        uintmax_t selectedSize = 0;
        int selectedCount = 0;
        
        for (const auto& item : cleanupItems) {
            totalSize += item.size;
            if (item.enabled) {
                selectedSize += item.size;
                selectedCount++;
            }
        }
        
        std::ostringstream oss;
        oss << "Selected: " << selectedCount << " items (" << FormatBytes(selectedSize) 
            << ") | Total: " << FormatBytes(totalSize);
        SetStatusText(oss.str());
    }

    // File I/O functions for custom directories
    void SaveCustomDirectories() {
        std::ofstream file("custom_dirs.txt");
        if (file.is_open()) {
            for (const auto& item : cleanupItems) {
                if (item.isCustom) {
                    file << item.name << "|" << item.path << "|" << item.description << "|" 
                         << (item.enabled ? "1" : "0") << std::endl;
                }
            }
            file.close();
        }
    }

    void LoadCustomDirectories() {
        std::ifstream file("custom_dirs.txt");
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                std::istringstream iss(line);
                std::string name, path, description, enabledStr;
                
                if (std::getline(iss, name, '|') && 
                    std::getline(iss, path, '|') && 
                    std::getline(iss, description, '|') && 
                    std::getline(iss, enabledStr)) {
                    
                    bool enabled = (enabledStr == "1");
                    if (fs::exists(path)) {
                        cleanupItems.push_back({name, path, description, enabled, false, true, 0});
                    }
                }
            }
            file.close();
        }
    }

    void AddCustomDirectory() {
        BROWSEINFO bi = {};
        bi.hwndOwner = hwndMain;
        bi.lpszTitle = L"Select a directory to add for cleanup:";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        
        LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
        if (pidl != nullptr) {
            wchar_t path[MAX_PATH];
            if (SHGetPathFromIDList(pidl, path)) {
                std::string pathStr = StringToString(std::wstring(path));
                
                // Check if directory already exists
                bool alreadyExists = false;
                for (const auto& item : cleanupItems) {
                    if (item.path == pathStr) {
                        alreadyExists = true;
                        break;
                    }
                }
                
                if (!alreadyExists) {
                    // Get directory name from path
                    std::string name = fs::path(pathStr).filename().string();
                    if (name.empty()) {
                        name = pathStr; // Use full path if no filename
                    }
                    
                    std::string description = "Custom directory: " + name;
                    cleanupItems.push_back({name, pathStr, description, true, false, true, 0});
                    
                    SaveCustomDirectories();
                    PopulateListView();
                    
                    // Calculate size for new directory
                    std::thread([this]() { CalculateSizesAsync(); }).detach();
                    
                    AppendToResults("Added custom directory: " + name);
                } else {
                    MessageBox(hwndMain, L"This directory is already in the cleanup list.", 
                              L"Directory Already Exists", MB_OK | MB_ICONINFORMATION);
                }
            }
            
            CoTaskMemFree(pidl);
        }
    }

    void RemoveSelectedDirectory() {
        int selectedIndex = ListView_GetNextItem(hwndListView, -1, LVNI_SELECTED);
        if (selectedIndex == -1) {
            MessageBox(hwndMain, L"Please select a directory to remove.", 
                      L"No Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }
        
        if (selectedIndex < static_cast<int>(cleanupItems.size())) {
            const auto& item = cleanupItems[selectedIndex];
            
            if (!item.isCustom) {
                MessageBox(hwndMain, L"Cannot remove built-in directories. Only custom directories can be removed.", 
                          L"Cannot Remove", MB_OK | MB_ICONWARNING);
                return;
            }
            
            std::wstring confirmMsg = L"Remove directory '" + StringToWString(item.name) + L"' from cleanup list?";
            if (MessageBox(hwndMain, confirmMsg.c_str(), L"Confirm Removal", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                AppendToResults("Removed custom directory: " + item.name);
                cleanupItems.erase(cleanupItems.begin() + selectedIndex);
                SaveCustomDirectories();
                PopulateListView();
                UpdateStatusBar();
            }
        }
    }

    // Helper function to convert wide string to string
    std::string StringToString(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
        result.resize(size - 1); // Remove null terminator
        return result;
    }

    void SetupCleanupItems() {
        std::string localAppData = GetEnvironmentVariable("LOCALAPPDATA");
        std::string appData = GetEnvironmentVariable("APPDATA");
        std::string userProfile = GetEnvironmentVariable("USERPROFILE");
        
        cleanupItems.clear();
        
        if (!localAppData.empty()) {
            std::string localTemp = localAppData + "\\Temp";
            if (fs::exists(localTemp)) {
                cleanupItems.push_back({"Local Temp", localTemp, "User temporary files", true, false, false, 0});
            }
        }

        cleanupItems.push_back({"Windows Temp", "C:\\Windows\\Temp", "System temporary files", true, true, false, 0});
        cleanupItems.push_back({"Prefetch", "C:\\Windows\\Prefetch", "Application prefetch files", true, true, false, 0});
        cleanupItems.push_back({"SoftwareDistribution", "C:\\Windows\\SoftwareDistribution\\Download", "Windows Update files", true, true, false, 0});
        
        if (!appData.empty()) {
            std::string recent = appData + "\\Microsoft\\Windows\\Recent";
            if (fs::exists(recent)) {
                cleanupItems.push_back({"Recent Items", recent, "Recently accessed files list", true, false, false, 0});
            }
        }

        cleanupItems.push_back({"Windows Logs", "C:\\Windows\\Logs", "System log files", true, true, false, 0});
        cleanupItems.push_back({"Error Reports", "C:\\ProgramData\\Microsoft\\Windows\\WER\\ReportQueue", "Windows Error Reports", true, true, false, 0});
        cleanupItems.push_back({"Memory Dumps", "C:\\Windows\\Minidump", "System crash dump files", true, true, false, 0});
        
        if (!localAppData.empty()) {
            cleanupItems.push_back({"Thumbnail Cache", localAppData + "\\Microsoft\\Windows\\Explorer", "Thumbnail cache files", true, false, false, 0});
        }
        
        cleanupItems.push_back({"Font Cache", "C:\\Windows\\System32\\FNTCACHE.DAT", "Windows font cache", true, true, false, 0});
        
        if (!localAppData.empty()) {
            std::vector<std::pair<std::string, std::string>> browsers = {
                {"Chrome Cache", localAppData + "\\Google\\Chrome\\User Data\\Default\\Cache"},
                {"Chrome Temp", localAppData + "\\Google\\Chrome\\User Data\\Default\\Local Storage"},
                {"Edge Cache", localAppData + "\\Microsoft\\Edge\\User Data\\Default\\Cache"},
                {"Firefox Cache", localAppData + "\\Mozilla\\Firefox\\Profiles"}
            };
            
            for (const auto& [name, path] : browsers) {
                if (fs::exists(path)) {
                    cleanupItems.push_back({name, path, "Browser cache and temporary files", false, false, false, 0});
                }
            }
        }

        cleanupItems.push_back({"IIS Logs", "C:\\inetpub\\logs\\LogFiles", "IIS web server logs", false, true, false, 0});
        cleanupItems.push_back({"Event Logs", "C:\\Windows\\System32\\winevt\\Logs", "Windows Event Logs (*.evtx)", false, true, false, 0});
        
        // Add Recycle Bin as a cleanup item
        cleanupItems.push_back({"Recycle Bin", "RECYCLE_BIN", "Files in Recycle Bin", true, false, false, 0});

        cleanupItems.erase(
            std::remove_if(cleanupItems.begin(), cleanupItems.end(),
                [](const CleanupItem& item) {
                    return !fs::exists(item.path) && item.path != "RECYCLE_BIN";
                }),
            cleanupItems.end()
        );
        
        // Load custom directories from file
        LoadCustomDirectories();
    }

    void PopulateListView() {
        ListView_DeleteAllItems(hwndListView);
        
        for (size_t i = 0; i < cleanupItems.size(); ++i) {
            const auto& item = cleanupItems[i];
            
            LVITEM lvi = {};
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = static_cast<int>(i);
            lvi.iSubItem = 0;
            
            std::wstring wname = StringToWString(item.name);
            lvi.pszText = const_cast<LPWSTR>(wname.c_str());
            lvi.lParam = i;
            
            int index = ListView_InsertItem(hwndListView, &lvi);
            ListView_SetCheckState(hwndListView, index, item.enabled);
            
            std::wstring wsize = StringToWString(FormatBytes(item.size));
            ListView_SetItemText(hwndListView, index, 1, const_cast<LPWSTR>(wsize.c_str()));
            
            std::string desc = item.description;
            if (item.isCustom) {
                desc = "🔧 " + desc; // Add wrench emoji for custom directories
            }
            std::wstring wdesc = StringToWString(desc);
            ListView_SetItemText(hwndListView, index, 2, const_cast<LPWSTR>(wdesc.c_str()));
            
            std::wstring wpath = StringToWString(item.path);
            ListView_SetItemText(hwndListView, index, 3, const_cast<LPWSTR>(wpath.c_str()));
        }
        
        UpdateStatusBar();
    }

    uintmax_t GetFolderSize(const std::string& folderPath) {
        uintmax_t totalSize = 0;
        std::error_code ec;
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(folderPath, 
                fs::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                
                if (entry.is_regular_file(ec) && !ec) {
                    auto fileSize = entry.file_size(ec);
                    if (!ec) {
                        totalSize += fileSize;
                    }
                }
            }
        } catch (const std::exception&) {
        }
        
        return totalSize;
    }

    // ULTRA-FAST folder size calculation - optimized for speed
    uintmax_t GetFolderSizeFast(const std::string& folderPath) {
        uintmax_t totalSize = 0;
        std::error_code ec;
        
        try {
            // Use faster iteration with minimal error checking for speed
            auto iter = fs::recursive_directory_iterator(folderPath, 
                fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink, ec);
            
            if (ec) return 0; // Quick exit on access error
            
            for (const auto& entry : iter) {
                // Fast path - minimal error checking for maximum speed
                try {
                    if (entry.is_regular_file()) {
                        totalSize += entry.file_size();
                    }
                } catch (...) {
                    // Ignore individual file errors for speed - keep going
                    continue;
                }
            }
        } catch (...) {
            // If anything fails, fall back to 0 - don't crash
            return 0;
        }
        
        return totalSize;
    }

    uintmax_t GetRecycleBinSize() {
        uintmax_t totalSize = 0;
        
        try {
            SHQUERYRBINFO sqrbi = {};
            sqrbi.cbSize = sizeof(SHQUERYRBINFO);
            
            HRESULT hr = SHQueryRecycleBinW(NULL, &sqrbi);
            
            if (SUCCEEDED(hr)) {
                totalSize = static_cast<uintmax_t>(sqrbi.i64Size);
            } else {
                std::vector<std::string> recycleBinPaths;
                
                DWORD drives = GetLogicalDrives();
                for (int i = 0; i < 26; i++) {
                    if (drives & (1 << i)) {
                        char driveLetter = 'A' + i;
                        std::string recyclePath = std::string(1, driveLetter) + ":\\$RECYCLE.BIN";
                        if (fs::exists(recyclePath)) {
                            recycleBinPaths.push_back(recyclePath);
                        }
                    }
                }
                
                for (const auto& path : recycleBinPaths) {
                    totalSize += GetFolderSize(path);
                }
            }
        } catch (const std::exception&) {
            totalSize = 0;
        }
        
        return totalSize;
    }

    void CalculateSizesAsync() {
        SetStatusText("Size calculation...");
        EnableWindow(hwndBtnRefresh, FALSE);
        
        std::atomic<int> completedItems{0};
        std::vector<std::thread> sizeThreads;
        
        // MAXIMUM PARALLEL SIZE CALCULATION - All directories simultaneously
        for (size_t i = 0; i < cleanupItems.size(); ++i) {
            sizeThreads.emplace_back([this, i, &completedItems]() {
                try {
                    if (cleanupItems[i].path == "RECYCLE_BIN") {
                        cleanupItems[i].size = GetRecycleBinSize();
                    } else {
                        cleanupItems[i].size = GetFolderSizeFast(cleanupItems[i].path);
                    }
                } catch (...) {
                    cleanupItems[i].size = 0; // Set to 0 on error, don't crash
                }
                
                completedItems++;
                
                // Update progress every few items to avoid UI spam
                if (completedItems.load() % 3 == 0 || completedItems.load() == static_cast<int>(cleanupItems.size())) {
                    UpdateProgress(completedItems.load(), static_cast<int>(cleanupItems.size()));
                }
            });
        }
        
        // Wait for all threads with timeout
        auto startTime = std::chrono::high_resolution_clock::now();
        for (auto& thread : sizeThreads) {
            if (thread.joinable()) {
                thread.join();
            }
            
            // Timeout protection - don't wait more than 30 seconds for size calculation
            auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 30) {
                // Detach remaining threads
                for (auto& remainingThread : sizeThreads) {
                    if (remainingThread.joinable()) {
                        remainingThread.detach();
                    }
                }
                break;
            }
        }
        
        PostMessage(hwndMain, WM_USER + 1, 0, 0);
        EnableWindow(hwndBtnRefresh, TRUE);
        SetStatusText("⚡ Size calculation complete!");
    }

    // Quick check if directory is empty or inaccessible
    bool IsDirectoryEmptyOrInaccessible(const std::string& folderPath) {
        if (folderPath == "RECYCLE_BIN") return false; // Special case
        
        try {
            if (!fs::exists(folderPath)) return true;
            if (!fs::is_directory(folderPath)) return true;
            
            std::error_code ec;
            auto it = fs::directory_iterator(folderPath, ec);
            if (ec) return true; // Inaccessible
            
            return it == fs::directory_iterator{}; // Empty if begin == end
        } catch (...) {
            return true; // Treat any exception as inaccessible
        }
    }

    CleanupResult DeleteFolderContentsParallel(const std::string& folderPath, const std::string& itemName) {
        auto startTime = std::chrono::high_resolution_clock::now();
        CleanupResult result{itemName, 0, 0, 0, true, "", std::chrono::milliseconds(0)};
        
        // Skip empty or inaccessible directories immediately
        if (IsDirectoryEmptyOrInaccessible(folderPath)) {
            AppendToResults(itemName + " - Skipped (empty or inaccessible)");
            auto endTime = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            return result;
        }
        
        // Handle Recycle Bin specially
        if (folderPath == "RECYCLE_BIN") {
            if (dryRunMode) {
                AppendToResults("[DRY RUN] Would empty Recycle Bin");
                result.bytesRemoved = GetRecycleBinSize();
                auto endTime = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                return result;
            } else {
                // Empty the recycle bin
                try {
                    uintmax_t sizeBefore = GetRecycleBinSize();
                    
                    // If recycle bin is empty, consider it successful
                    if (sizeBefore == 0) {
                        result.bytesRemoved = 0;
                        result.filesDeleted = 0;
                        result.success = true;
                        AppendToResults("Recycle Bin is already empty");
                    } else {
                        DWORD flags = 0x00000001 | 0x00000002 | 0x00000004; // SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND
                        HRESULT hr = SHEmptyRecycleBinW(NULL, NULL, flags);
                        
                        if (SUCCEEDED(hr)) {
                            result.bytesRemoved = sizeBefore;
                            result.filesDeleted = 1; // We don't have exact file count, so use 1 to indicate success
                            result.success = true;
                            AppendToResults("Recycle Bin emptied successfully - " + FormatBytes(sizeBefore) + " freed");
                        } else {
                            // Even if it fails, mark as success if it's just because it's empty or similar minor issue
                            if (hr == 0x8000ffff || hr == S_FALSE) {
                                result.bytesRemoved = 0;
                                result.filesDeleted = 0;
                                result.success = true;
                                AppendToResults("Recycle Bin cleanup completed (may have been empty)");
                            } else {
                                result.success = false;
                                std::ostringstream oss;
                                oss << "Failed to empty Recycle Bin. Error code: 0x" << std::hex << hr;
                                result.errorMessage = oss.str();
                                AppendToResults(result.errorMessage);
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    result.success = false;
                    result.errorMessage = "Error emptying Recycle Bin: " + std::string(e.what());
                    AppendToResults(result.errorMessage);
                }
                
                auto endTime = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                return result;
            }
        }
        
        if (dryRunMode) {
            AppendToResults("[DRY RUN] Would clean: " + itemName);
            result.bytesRemoved = GetFolderSize(folderPath);
            auto endTime = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            return result;
        }

        uintmax_t sizeBefore = GetFolderSize(folderPath);
        std::error_code ec;
        
        try {
            std::vector<fs::path> itemsToDelete;
            
            for (const auto& entry : fs::directory_iterator(folderPath, ec)) {
                if (!ec) {
                    itemsToDelete.push_back(entry.path());
                }
            }
            
            // ULTRA-HIGH-SPEED file deletion with maximum parallelization
            const size_t maxThreads = std::thread::hardware_concurrency() * 2; // Use more threads for I/O bound operations
            const size_t batchSize = (std::max)(size_t(1), itemsToDelete.size() / maxThreads);
            
            std::vector<std::thread> deleteThreads;
            std::atomic<int> deleted{0}, skipped{0};
            
            // Launch maximum parallel deletion threads
            for (size_t i = 0; i < itemsToDelete.size(); i += batchSize) {
                size_t endIdx = (std::min)(i + batchSize, itemsToDelete.size());
                
                deleteThreads.emplace_back([this, &itemsToDelete, i, endIdx, &deleted, &skipped]() {
                    int localDeleted = 0, localSkipped = 0;
                    
                    for (size_t j = i; j < endIdx; ++j) {
                        const auto& item = itemsToDelete[j];
                        std::error_code ec;
                        
                        try {
                            if (fs::is_regular_file(item, ec) && !ec) {
                                if (fs::remove(item, ec) && !ec) {
                                    localDeleted++;
                                } else {
                                    localSkipped++;
                                }
                            } else if (fs::is_directory(item, ec) && !ec) {
                                auto removed = fs::remove_all(item, ec);
                                if (!ec && removed > 0) {
                                    localDeleted += static_cast<int>(removed);
                                } else {
                                    localSkipped++;
                                }
                            } else {
                                localSkipped++;
                            }
                        } catch (...) {
                            localSkipped++;
                        }
                    }
                    
                    // Update atomics once per batch for performance
                    deleted += localDeleted;
                    skipped += localSkipped;
                });
            }
            
            // Wait for all deletion threads with timeout
            auto deleteStart = std::chrono::high_resolution_clock::now();
            for (auto& thread : deleteThreads) {
                if (thread.joinable()) {
                    thread.join();
                }
                
                // Safety timeout - if deletion takes too long, move on
                auto elapsed = std::chrono::high_resolution_clock::now() - deleteStart;
                if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 30) {
                    // Detach remaining threads and continue
                    for (auto& remainingThread : deleteThreads) {
                        if (remainingThread.joinable()) {
                            remainingThread.detach();
                        }
                    }
                    break;
                }
            }
            
            result.filesDeleted = deleted.load();
            result.filesSkipped = skipped.load();
            
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = e.what();
            AppendToResults("Exception in deleteFolderContents: " + std::string(e.what()));
        }
        
        uintmax_t sizeAfter = GetFolderSize(folderPath);
        result.bytesRemoved = sizeBefore - sizeAfter;
        
        AppendToResults(itemName + " - Deleted: " + std::to_string(result.filesDeleted) + 
                       " items, Skipped: " + std::to_string(result.filesSkipped) + " items");
        
        auto endTime = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        return result;
    }

    void EmptyRecycleBin() {
        if (dryRunMode) {
            AppendToResults("[DRY RUN] Would empty Recycle Bin");
            return;
        }

        try {
            DWORD flags = 0x00000001 | 0x00000002 | 0x00000004;
            HRESULT result = SHEmptyRecycleBinW(NULL, NULL, flags);
            
            if (SUCCEEDED(result)) {
                AppendToResults("Recycle Bin cleaned successfully.");
            } else {
                std::ostringstream oss;
                oss << "Failed to clean Recycle Bin. Error code: 0x" << std::hex << result;
                AppendToResults(oss.str());
            }
        } catch (const std::exception& e) {
            AppendToResults("Error cleaning Recycle Bin: " + std::string(e.what()));
        }
    }

    void StartParallelCleanup() {
        std::vector<CleanupItem> selectedItems;
        for (const auto& item : cleanupItems) {
            if (item.enabled) {
                selectedItems.push_back(item);
            }
        }

        if (selectedItems.empty()) {
            MessageBox(hwndMain, L"No items selected for cleanup.", L"Warning", MB_OK | MB_ICONWARNING);
            return;
        }

        // Pre-filter: Remove empty or inaccessible directories
        AppendToResults("Pre-checking selected directories...");
        std::vector<CleanupItem> validItems;
        for (const auto& item : selectedItems) {
            if (IsDirectoryEmptyOrInaccessible(item.path)) {
                AppendToResults(item.name + " - Skipped (empty or inaccessible)");
            } else {
                validItems.push_back(item);
                AppendToResults(item.name + " - Ready for cleanup");
            }
        }
        
        if (validItems.empty()) {
            AppendToResults("No valid directories found for cleanup.");
            MessageBox(hwndMain, L"All selected directories are empty or inaccessible.", L"Info", MB_OK | MB_ICONINFORMATION);
            isCleanupRunning = false;
            EnableWindow(hwndBtnCleanup, TRUE);
            EnableWindow(hwndBtnRefresh, TRUE);
            return;
        }
        
        selectedItems = validItems; // Use only the valid items

        uintmax_t totalSelectedSize = 0;
        for (const auto& item : selectedItems) {
            totalSelectedSize += item.size;
        }

        std::wstring confirmMsg = L"About to clean " + std::to_wstring(selectedItems.size()) + 
                                 L" locations (" + StringToWString(FormatBytes(totalSelectedSize)) + L").\n\n";
        
        if (!dryRunMode) {
            confirmMsg += L"WARNING: This will permanently delete files!\n\n";
        }
        
        confirmMsg += L"Continue?";
        
        if (MessageBox(hwndMain, confirmMsg.c_str(), L"Confirm Cleanup", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }

        isCleanupRunning = true;
        EnableWindow(hwndBtnCleanup, FALSE);
        EnableWindow(hwndBtnRefresh, FALSE);
        
        SetWindowText(hwndResults, L"");

        completedTasks = 0;
        totalTasks = static_cast<int>(selectedItems.size());
        SendMessage(hwndProgressOverall, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(hwndProgressOverall, PBM_SETPOS, 0, 0);

        auto startTime = std::chrono::high_resolution_clock::now();
        
        const unsigned int numThreads = std::thread::hardware_concurrency();
        const unsigned int maxConcurrent = (std::min)(static_cast<unsigned int>(selectedItems.size()), numThreads);
        
        AppendToResults("🚀 DiskCleaner " + GetVersionString() + " TURBO - Ultra-fast parallel cleanup");
        AppendToResults("⚡ Maximum performance mode : " + std::to_string(maxConcurrent) + " threads + detached execution");
        AppendToResults("🛡️ Administrator privileges active - all system locations accessible");
        AppendToResults("📊 Total tasks to process : " + std::to_string(totalTasks.load()));
        
        std::vector<CleanupResult> results;
        std::atomic<int> completedCount{0};
        
        AppendToResults("Processing ALL " + std::to_string(selectedItems.size()) + " tasks in parallel for maximum speed!");
        
        std::vector<std::shared_ptr<bool>> taskDoneFlags(selectedItems.size());
        std::vector<std::shared_ptr<CleanupResult>> taskResults(selectedItems.size());
        std::vector<std::shared_ptr<std::mutex>> taskMutexes(selectedItems.size());
        
        for (size_t i = 0; i < selectedItems.size(); ++i) {
            taskDoneFlags[i] = std::make_shared<bool>(false);
            taskResults[i] = std::make_shared<CleanupResult>();
            taskMutexes[i] = std::make_shared<std::mutex>();
        }
        
        AppendToResults("⚡ Launching " + std::to_string(selectedItems.size()) + " parallel cleanup threads...");
        for (size_t i = 0; i < selectedItems.size(); ++i) {
            const auto& item = selectedItems[i];
            auto taskDone = taskDoneFlags[i];
            auto taskResult = taskResults[i];
            auto taskMutex = taskMutexes[i];
            
            std::thread([this, item, taskDone, taskResult, taskMutex]() {
                try {
                    CleanupResult result = DeleteFolderContentsParallel(item.path, item.name);
                    std::lock_guard<std::mutex> lock(*taskMutex);
                    *taskResult = result;
                    *taskDone = true;
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(*taskMutex);
                    taskResult->itemName = item.name;
                    taskResult->success = false;
                    taskResult->errorMessage = "Exception: " + std::string(e.what());
                    taskResult->bytesRemoved = 0;
                    taskResult->filesDeleted = 0;
                    taskResult->filesSkipped = 0;
                    *taskDone = true;
                } catch (...) {
                    std::lock_guard<std::mutex> lock(*taskMutex);
                    taskResult->itemName = item.name;
                    taskResult->success = false;
                    taskResult->errorMessage = "Unknown exception";
                    taskResult->bytesRemoved = 0;
                    taskResult->filesDeleted = 0;
                    taskResult->filesSkipped = 0;
                    *taskDone = true;
                }
            }).detach();
        }
        
        AppendToResults("⚡ All tasks launched! Monitoring completion...");
        
        std::vector<bool> taskCompleted(selectedItems.size(), false);
        auto monitoringStartTime = std::chrono::high_resolution_clock::now();
        
        while (completedCount.load() < static_cast<int>(selectedItems.size())) {
            bool anyProgress = false;
            
            for (size_t i = 0; i < selectedItems.size(); ++i) {
                if (taskCompleted[i]) continue;
                
                {
                    std::lock_guard<std::mutex> lock(*taskMutexes[i]);
                    if (*taskDoneFlags[i]) {
                        results.push_back(*taskResults[i]);
                        completedCount++;
                        taskCompleted[i] = true;
                        anyProgress = true;
                        
                        if (verboseMode) {
                            AppendToResults("✅ " + taskResults[i]->itemName + " (" + std::to_string(completedCount.load()) + "/" + std::to_string(totalTasks.load()) + ")");
                        }
                        UpdateProgress(completedCount.load(), totalTasks.load());
                    }
                }
            }
            
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - monitoringStartTime);
            
            if (elapsed.count() > 15) {
                for (size_t i = 0; i < selectedItems.size(); ++i) {
                    if (!taskCompleted[i]) {
                        AppendToResults("⚠️ TIMEOUT: " + selectedItems[i].name + " (thread continues in background)");
                        
                        CleanupResult timeoutResult;
                        timeoutResult.itemName = selectedItems[i].name;
                        timeoutResult.success = false;
                        timeoutResult.errorMessage = "Timeout";
                        timeoutResult.bytesRemoved = 0;
                        timeoutResult.filesDeleted = 0;
                        timeoutResult.filesSkipped = 0;
                        results.push_back(timeoutResult);
                        
                        taskCompleted[i] = true;
                        completedCount++;
                        UpdateProgress(completedCount.load(), totalTasks.load());
                    }
                }
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);

        uintmax_t totalRemoved = 0;
        int totalFilesDeleted = 0;
        int totalFilesSkipped = 0;
        int successfulOperations = 0;
        
        for (const auto& result : results) {
            totalRemoved += result.bytesRemoved;
            totalFilesDeleted += result.filesDeleted;
            totalFilesSkipped += result.filesSkipped;
            if (result.success) successfulOperations++;
        }

        AppendToResults("");
        AppendToResults("=== Cleanup Summary ===");
        AppendToResults("Total space " + std::string(dryRunMode ? "that would be " : "") + "freed: " + FormatBytes(totalRemoved));
        AppendToResults("Files deleted: " + std::to_string(totalFilesDeleted));
        AppendToResults("Files skipped: " + std::to_string(totalFilesSkipped));
        AppendToResults("Successful operations: " + std::to_string(successfulOperations) + "/" + std::to_string(results.size()));
        AppendToResults("Total time: " + std::to_string(totalDuration.count()) + " seconds");
        
        if (!results.empty()) {
            auto avgTimePerTask = totalDuration.count() / static_cast<double>(results.size());
            std::ostringstream oss;
            oss << "Average time per location: " << std::fixed << std::setprecision(2) << avgTimePerTask << " seconds";
            AppendToResults(oss.str());
        }
        
        isCleanupRunning = false;
        EnableWindow(hwndBtnCleanup, TRUE);
        EnableWindow(hwndBtnRefresh, TRUE);
        SetStatusText("Cleanup completed.");
        
        std::thread([this]() { CalculateSizesAsync(); }).detach();
    }

public:
    bool Initialize(HINSTANCE hInstance) {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"DiskCleaner";
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
        
        if (!RegisterClassEx(&wc)) {
            DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS) {
                return false;
            }
        }
        
        hwndMain = CreateWindowEx(
            0,
            L"DiskCleaner",
            GetAppTitle().c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            WINDOW_WIDTH, WINDOW_HEIGHT,
            nullptr, nullptr, hInstance, this
        );
        
        if (!hwndMain) {
            return false;
        }
        
        ShowWindow(hwndMain, SW_SHOW);
        UpdateWindow(hwndMain);
        
        return true;
    }
    
    void MessageLoop() {
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
};

void HideConsole() {
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow) {
        ShowWindow(consoleWindow, SW_HIDE);
    }
    
    if (GetConsoleWindow()) {
        FreeConsole();
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    HideConsole();
    
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    
    if (!isAdmin) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        
        int result = MessageBox(nullptr, 
            L"This application requires administrator privileges to clean system files.\n\n"
            L"Click OK to restart with administrator privileges, or Cancel to exit.",
            L"Administrator Privileges Required", 
            MB_OKCANCEL | MB_ICONINFORMATION);
            
        if (result == IDOK) {
            SHELLEXECUTEINFOW sei = {};
            sei.cbSize = sizeof(sei);
            sei.lpVerb = L"runas";
            sei.lpFile = exePath;
            sei.hwnd = nullptr;
            sei.nShow = SW_NORMAL;
            
            if (ShellExecuteExW(&sei)) {
                return 0;
            } else {
                MessageBox(nullptr, L"Failed to elevate privileges. The application will exit.", 
                          L"Error", MB_OK | MB_ICONERROR);
                return 1;
            }
        } else {
            return 0;
        }
    }
    
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&icex)) {
        MessageBox(nullptr, L"Failed to initialize common controls.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    DiskCleanerGUI app;
    if (!app.Initialize(hInstance)) {
        DWORD error = GetLastError();
        wchar_t buffer[256];
        swprintf_s(buffer, L"Failed to initialize application.\nError code: %d", error);
        MessageBox(nullptr, buffer, L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    app.MessageLoop();
    return 0;
} 