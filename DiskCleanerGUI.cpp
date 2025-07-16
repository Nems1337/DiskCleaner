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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

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

struct CleanupItem {
    std::string name;
    std::string path;
    std::string description;
    bool enabled;
    bool requiresAdmin;
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
                HandleCommand(LOWORD(wParam));
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
                
            default:
                return DefWindowProc(hwndMain, uMsg, wParam, lParam);
        }
    }

    void CreateControls() {
        hwndListView = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEW,
            L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
            10, 10, 600, 350,
            hwndMain, (HMENU)ID_LISTVIEW, GetModuleHandle(nullptr), nullptr
        );
        
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
            620, 150, 100, 40, hwndMain, (HMENU)ID_BTN_CLEANUP, GetModuleHandle(nullptr), nullptr);
            
        hwndBtnRecycleBin = CreateWindow(L"BUTTON", L"Empty Recycle Bin",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            620, 200, 100, 30, hwndMain, (HMENU)ID_BTN_RECYCLEBIN, GetModuleHandle(nullptr), nullptr);

        // Create checkboxes
        hwndChkDryRun = CreateWindow(L"BUTTON", L"Dry Run Mode",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            620, 250, 120, 20, hwndMain, (HMENU)ID_CHK_DRYRUN, GetModuleHandle(nullptr), nullptr);
            
        hwndChkVerbose = CreateWindow(L"BUTTON", L"Verbose Mode",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            620, 280, 120, 20, hwndMain, (HMENU)ID_CHK_VERBOSE, GetModuleHandle(nullptr), nullptr);

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

    void SetStatusText(const std::string& text) {
        std::wstring wtext = StringToWString(text);
        SetWindowText(hwndStatus, wtext.c_str());
    }

    void AppendToResults(const std::string& text) {
        std::lock_guard<std::mutex> lock(logMutex);
        
        // Get current text length
        int length = GetWindowTextLength(hwndResults);
        
        // Move to end and append new text
        SendMessage(hwndResults, EM_SETSEL, length, length);
        std::wstring wtext = StringToWString(text + "\r\n");
        SendMessage(hwndResults, EM_REPLACESEL, FALSE, (LPARAM)wtext.c_str());
        
        // Auto-scroll to bottom
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

    void SetupCleanupItems() {
        std::string localAppData = GetEnvironmentVariable("LOCALAPPDATA");
        std::string appData = GetEnvironmentVariable("APPDATA");
        std::string userProfile = GetEnvironmentVariable("USERPROFILE");
        
        cleanupItems.clear();
        
        if (!localAppData.empty()) {
            std::string localTemp = localAppData + "\\Temp";
            if (fs::exists(localTemp)) {
                cleanupItems.push_back({"Local Temp", localTemp, "User temporary files", true, false, 0});
            }
        }

        cleanupItems.push_back({"Windows Temp", "C:\\Windows\\Temp", "System temporary files", true, true, 0});
        cleanupItems.push_back({"Prefetch", "C:\\Windows\\Prefetch", "Application prefetch files", true, true, 0});
        cleanupItems.push_back({"SoftwareDistribution", "C:\\Windows\\SoftwareDistribution\\Download", "Windows Update files", true, true, 0});
        
        if (!appData.empty()) {
            std::string recent = appData + "\\Microsoft\\Windows\\Recent";
            if (fs::exists(recent)) {
                cleanupItems.push_back({"Recent Items", recent, "Recently accessed files list", true, false, 0});
            }
        }

        cleanupItems.push_back({"Windows Logs", "C:\\Windows\\Logs", "System log files", true, true, 0});
        cleanupItems.push_back({"Error Reports", "C:\\ProgramData\\Microsoft\\Windows\\WER\\ReportQueue", "Windows Error Reports", true, true, 0});
        cleanupItems.push_back({"Memory Dumps", "C:\\Windows\\Minidump", "System crash dump files", true, true, 0});
        
        if (!localAppData.empty()) {
            cleanupItems.push_back({"Thumbnail Cache", localAppData + "\\Microsoft\\Windows\\Explorer", "Thumbnail cache files", true, false, 0});
        }
        
        cleanupItems.push_back({"Font Cache", "C:\\Windows\\System32\\FNTCACHE.DAT", "Windows font cache", true, true, 0});
        
        if (!localAppData.empty()) {
            std::vector<std::pair<std::string, std::string>> browsers = {
                {"Chrome Cache", localAppData + "\\Google\\Chrome\\User Data\\Default\\Cache"},
                {"Chrome Temp", localAppData + "\\Google\\Chrome\\User Data\\Default\\Local Storage"},
                {"Edge Cache", localAppData + "\\Microsoft\\Edge\\User Data\\Default\\Cache"},
                {"Firefox Cache", localAppData + "\\Mozilla\\Firefox\\Profiles"}
            };
            
            for (const auto& [name, path] : browsers) {
                if (fs::exists(path)) {
                    cleanupItems.push_back({name, path, "Browser cache and temporary files", false, false, 0});
                }
            }
        }

        cleanupItems.push_back({"IIS Logs", "C:\\inetpub\\logs\\LogFiles", "IIS web server logs", false, true, 0});
        cleanupItems.push_back({"Event Logs", "C:\\Windows\\System32\\winevt\\Logs", "Windows Event Logs (*.evtx)", false, true, 0});
        
        // Add Recycle Bin as a cleanup item
        cleanupItems.push_back({"Recycle Bin", "RECYCLE_BIN", "Files in Recycle Bin", true, false, 0});

        cleanupItems.erase(
            std::remove_if(cleanupItems.begin(), cleanupItems.end(),
                [](const CleanupItem& item) {
                    return !fs::exists(item.path) && item.path != "RECYCLE_BIN";
                }),
            cleanupItems.end()
        );
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
            if (item.requiresAdmin && !IsAdmin()) {
                desc += " (Admin required)";
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

    uintmax_t GetRecycleBinSize() {
        uintmax_t totalSize = 0;
        
        try {
            // Get the recycle bin size using Windows API
            SHQUERYRBINFO sqrbi = {};
            sqrbi.cbSize = sizeof(SHQUERYRBINFO);
            
            HRESULT hr = SHQueryRecycleBinW(NULL, &sqrbi);
            
            if (SUCCEEDED(hr)) {
                totalSize = static_cast<uintmax_t>(sqrbi.i64Size);
            } else {
                // Fallback: Try to calculate size manually by checking known recycle bin paths
                std::vector<std::string> recycleBinPaths;
                
                // Try to get all drives
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
                
                // Calculate total size from all recycle bin folders
                for (const auto& path : recycleBinPaths) {
                    totalSize += GetFolderSize(path);
                }
            }
        } catch (const std::exception&) {
            // If all else fails, return 0
            totalSize = 0;
        }
        
        return totalSize;
    }

    void CalculateSizesAsync() {
        SetStatusText("Calculating sizes...");
        EnableWindow(hwndBtnRefresh, FALSE);
        
        const unsigned int numThreads = std::thread::hardware_concurrency();
        std::vector<std::future<void>> futures;
        std::atomic<int> completedItems{0};
        
        for (size_t i = 0; i < cleanupItems.size(); ++i) {
            auto future = std::async(std::launch::async, [this, i, &completedItems]() {
                if (cleanupItems[i].path == "RECYCLE_BIN") {
                    cleanupItems[i].size = GetRecycleBinSize();
                } else {
                    cleanupItems[i].size = GetFolderSize(cleanupItems[i].path);
                }
                completedItems++;
                UpdateProgress(completedItems.load(), static_cast<int>(cleanupItems.size()));
            });
            
            futures.push_back(std::move(future));
            
            if (futures.size() >= numThreads) {
                for (auto& f : futures) {
                    f.wait();
                }
                futures.clear();
            }
        }
        
        for (auto& future : futures) {
            future.wait();
        }
        
        PostMessage(hwndMain, WM_USER + 1, 0, 0);
        EnableWindow(hwndBtnRefresh, TRUE);
        SetStatusText("Size calculation complete.");
    }

    CleanupResult DeleteFolderContentsParallel(const std::string& folderPath, const std::string& itemName) {
        auto startTime = std::chrono::high_resolution_clock::now();
        CleanupResult result{itemName, 0, 0, 0, true, "", std::chrono::milliseconds(0)};
        
        // Handle Recycle Bin specially
        if (folderPath == "RECYCLE_BIN") {
            if (dryRunMode) {
                AppendToResults("[DRY RUN] Would empty Recycle Bin");
                result.bytesRemoved = GetRecycleBinSize();
                completedTasks++;
                UpdateProgress(completedTasks.load(), totalTasks.load());
                auto endTime = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                return result;
            } else {
                // Empty the recycle bin
                try {
                    uintmax_t sizeBefore = GetRecycleBinSize();
                    DWORD flags = 0x00000001 | 0x00000002 | 0x00000004; // SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND
                    HRESULT hr = SHEmptyRecycleBinW(NULL, NULL, flags);
                    
                    if (SUCCEEDED(hr)) {
                        result.bytesRemoved = sizeBefore;
                        result.filesDeleted = 1; // We don't have exact file count, so use 1 to indicate success
                        result.success = true;
                        AppendToResults("Recycle Bin emptied successfully - " + FormatBytes(sizeBefore) + " freed");
                    } else {
                        result.success = false;
                        std::ostringstream oss;
                        oss << "Failed to empty Recycle Bin. Error code: 0x" << std::hex << hr;
                        result.errorMessage = oss.str();
                        AppendToResults(result.errorMessage);
                    }
                } catch (const std::exception& e) {
                    result.success = false;
                    result.errorMessage = "Error emptying Recycle Bin: " + std::string(e.what());
                    AppendToResults(result.errorMessage);
                }
                
                completedTasks++;
                UpdateProgress(completedTasks.load(), totalTasks.load());
                auto endTime = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                return result;
            }
        }
        
        if (dryRunMode) {
            AppendToResults("[DRY RUN] Would clean: " + itemName);
            result.bytesRemoved = GetFolderSize(folderPath);
            completedTasks++;
            UpdateProgress(completedTasks.load(), totalTasks.load());
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
            
            const size_t batchSize = (std::max)(size_t(1), itemsToDelete.size() / std::thread::hardware_concurrency());
            std::vector<std::future<std::pair<int, int>>> futures;
            
            for (size_t i = 0; i < itemsToDelete.size(); i += batchSize) {
                size_t endIdx = (std::min)(i + batchSize, itemsToDelete.size());
                
                auto future = std::async(std::launch::async, [this, &itemsToDelete, i, endIdx, &itemName]() {
                    int deleted = 0, skipped = 0;
                    
                    for (size_t j = i; j < endIdx; ++j) {
                        const auto& item = itemsToDelete[j];
                        std::error_code ec;
                        
                        try {
                            if (fs::is_regular_file(item, ec) && !ec) {
                                if (fs::remove(item, ec) && !ec) {
                                    deleted++;
                                    if (verboseMode) {
                                        AppendToResults("Deleted file: " + item.string());
                                    }
                                } else {
                                    skipped++;
                                    if (verboseMode) {
                                        AppendToResults("Skipped file: " + item.string() + " - " + ec.message());
                                    }
                                }
                            } else if (fs::is_directory(item, ec) && !ec) {
                                auto removed = fs::remove_all(item, ec);
                                if (!ec && removed > 0) {
                                    deleted += static_cast<int>(removed);
                                    if (verboseMode) {
                                        AppendToResults("Deleted directory: " + item.string() + " (" + std::to_string(removed) + " items)");
                                    }
                                } else {
                                    skipped++;
                                    if (verboseMode) {
                                        AppendToResults("Skipped directory: " + item.string() + " - " + ec.message());
                                    }
                                }
                            }
                            
                            std::this_thread::sleep_for(std::chrono::microseconds(100));
                            
                        } catch (const std::exception& e) {
                            skipped++;
                            if (verboseMode) {
                                AppendToResults("Exception deleting: " + item.string() + " - " + e.what());
                            }
                        }
                    }
                    
                    return std::make_pair(deleted, skipped);
                });
                
                futures.push_back(std::move(future));
            }
            
            for (auto& future : futures) {
                try {
                    auto [deleted, skipped] = future.get();
                    result.filesDeleted += deleted;
                    result.filesSkipped += skipped;
                } catch (const std::exception& e) {
                    result.success = false;
                    result.errorMessage = e.what();
                }
            }
            
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = e.what();
            AppendToResults("Exception in deleteFolderContents: " + std::string(e.what()));
        }
        
        uintmax_t sizeAfter = GetFolderSize(folderPath);
        result.bytesRemoved = sizeBefore - sizeAfter;
        
        AppendToResults(itemName + " - Deleted: " + std::to_string(result.filesDeleted) + 
                       " items, Skipped: " + std::to_string(result.filesSkipped) + " items");
        
        completedTasks++;
        UpdateProgress(completedTasks.load(), totalTasks.load());
        
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
        bool needsAdmin = false;
        for (const auto& item : cleanupItems) {
            if (item.enabled && item.requiresAdmin && !IsAdmin()) {
                needsAdmin = true;
                break;
            }
        }
        
        if (needsAdmin) {
            MessageBox(hwndMain, L"Some selected items require administrator privileges. The application will restart with elevated permissions.", 
                      L"Admin Required", MB_OK | MB_ICONINFORMATION);
            RelaunchAsAdmin();
            return;
        }

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
        
        AppendToResults("Starting parallel cleanup with " + std::to_string(maxConcurrent) + " threads...");
        
        std::vector<std::future<CleanupResult>> futures;
        
        for (const auto& item : selectedItems) {
            auto future = std::async(std::launch::async, [this, item]() {
                return DeleteFolderContentsParallel(item.path, item.name);
            });
            
            futures.push_back(std::move(future));
            
            if (futures.size() >= maxConcurrent) {
                for (auto it = futures.begin(); it != futures.end(); ) {
                    if (it->wait_for(std::chrono::milliseconds(10)) == std::future_status::ready) {
                        it = futures.erase(it);
                        break;
                    } else {
                        ++it;
                    }
                }
            }
        }

        std::vector<CleanupResult> results;
        for (auto& future : futures) {
            try {
                results.push_back(future.get());
            } catch (const std::exception& e) {
                AppendToResults("Error getting cleanup result: " + std::string(e.what()));
            }
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
            return false;
        }
        
        hwndMain = CreateWindowEx(
            0,
            L"DiskCleaner",
            L"DiskCleaner v2.1",
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);
    
    DiskCleanerGUI app;
    if (!app.Initialize(hInstance)) {
        MessageBox(nullptr, L"Failed to initialize application.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    app.MessageLoop();
    return 0;
} 