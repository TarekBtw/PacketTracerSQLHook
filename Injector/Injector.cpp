#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

const wchar_t* TARGET_PROC_NAME = L"firefox.exe";
const char*    DLL_PATH         = "firefoxOverlay.dll";

#define DBGPRINT(fmt, ...) do { \
char dbgprint_buf[512]; \
snprintf(dbgprint_buf, sizeof(dbgprint_buf), fmt, __VA_ARGS__); \
OutputDebugStringA(dbgprint_buf); \
} while(0)

DWORD FindProcessId(const std::wstring& procName) {
    PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD pid = 0;
    if (hSnap != INVALID_HANDLE_VALUE) {
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (procName == pe.szExeFile) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return pid;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD)lParam && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        *(HWND*)(&lParam) = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindMainWindow(DWORD pid) {
    HWND hwnd = NULL;
    LPARAM lParam = (LPARAM)pid;
    EnumWindows(EnumWindowsProc, lParam);
    return (HWND)lParam;
}

bool InjectDll(DWORD pid, const std::string& dllPath) {
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) { DBGPRINT("[#] OpenProcess failed\n"); return false; }
    LPVOID mem = VirtualAllocEx(hProc, nullptr, dllPath.size() + 1, MEM_COMMIT, PAGE_READWRITE);
    if (!mem) { DBGPRINT("[#] VirtualAllocEx failed\n"); CloseHandle(hProc); return false; }
    WriteProcessMemory(hProc, mem, dllPath.c_str(), dllPath.size() + 1, nullptr);
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
        (LPTHREAD_START_ROUTINE)LoadLibraryA, mem, 0, nullptr);
    if (!hThread) { DBGPRINT("[#] CreateRemoteThread failed\n"); }
    else { DBGPRINT("[#] Injected DLL!\n"); WaitForSingleObject(hThread, INFINITE); CloseHandle(hThread); }
    VirtualFreeEx(hProc, mem, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return true;
}

bool CheckFileExists(const char* filename) {
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
        return true;
    }
    return false;
}

bool InstallPythonRequirements() {
    DBGPRINT("[#] Installing Python requirements...\n");
    DBGPRINT("[#] User: TarekBtw | Time: 2025-06-06 13:49:23 UTC\n");
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    
    // Try installing from requirements.txt first
    std::string cmd;
    if (CheckFileExists("requirements.txt")) {
        cmd = "pip install -r requirements.txt";
        DBGPRINT("[#] Found requirements.txt, installing from file...\n");
    } else {
        cmd = "pip install requests beautifulsoup4 lxml";
        DBGPRINT("[#] No requirements.txt found, installing packages directly...\n");
    }
    
    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 
                      CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        
        DBGPRINT("[#] Python requirements installation launched.\n");
        
        // Wait for installation (max 2 minutes)
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 120000);
        
        if (waitResult == WAIT_OBJECT_0) {
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            DBGPRINT("[#] Requirements installation completed with exit code: %lu\n", exitCode);
            
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return (exitCode == 0);
        } else if (waitResult == WAIT_TIMEOUT) {
            DBGPRINT("[#] Requirements installation timed out\n");
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    } else {
        DWORD err = GetLastError();
        DBGPRINT("[#] Failed to launch pip install (Error %u)\n", err);
        return false;
    }
}

bool RunPythonTextExtractor() {
    DBGPRINT("[#] Running CCNA Python text extractor...\n");
    DBGPRINT("[#] User: TarekBtw | Time: 2025-06-06 13:49:23 UTC\n");
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    
    std::string cmd = "python ccna_text_extractor.py";
    
    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 
                      CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        
        DBGPRINT("[#] Python text extractor launched successfully.\n");
        
        // Wait for Python script to complete (max 5 minutes)
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 300000);
        
        if (waitResult == WAIT_OBJECT_0) {
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            DBGPRINT("[#] Python text extractor completed with exit code: %lu\n", exitCode);
            
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return (exitCode == 0);
        } else if (waitResult == WAIT_TIMEOUT) {
            DBGPRINT("[#] Python text extractor timed out (5 minutes)\n");
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    } else {
        DWORD err = GetLastError();
        DBGPRINT("[#] Failed to launch Python text extractor (Error %u)\n", err);
        return false;
    }
}

bool SetupPythonEnvironment() {
    DBGPRINT("[#] Setting up Python environment...\n");
    
    // Step 1: Install requirements
    if (!InstallPythonRequirements()) {
        DBGPRINT("[#] Requirements installation failed, but continuing...\n");
        // Continue anyway, maybe requirements are already installed
    } else {
        DBGPRINT("[#] Requirements installed successfully!\n");
    }
    
    // Step 2: Run extractor
    if (!RunPythonTextExtractor()) {
        DBGPRINT("[#] Python extraction failed.\n");
        return false;
    }
    
    DBGPRINT("[#] Python setup completed successfully!\n");
    return true;
}

int main()
{
    DBGPRINT("[#] CCNA Injector Started\n");
    DBGPRINT("[#] User: TarekBtw | Time: 2025-06-06 13:49:23 UTC\n");
    DBGPRINT("[#] ========================================\n");
    
    // Step 1: Check if text file exists
    if (!CheckFileExists("ccna_questions.txt")) {
        DBGPRINT("[#] CCNA text file not found. Setting up Python environment...\n");
        if (!SetupPythonEnvironment()) {
            DBGPRINT("[#] Python setup failed. Continuing anyway...\n");
        }
    } else {
        DBGPRINT("[#] CCNA text file found. Asking user for update...\n");
        
        std::cout << "\nCCNA questions file found. Do you want to update it? (y/n): ";
        char choice;
        std::cin >> choice;
        
        if (choice == 'y' || choice == 'Y') {
            DBGPRINT("[#] User chose to update file. Setting up Python environment...\n");
            if (!SetupPythonEnvironment()) {
                DBGPRINT("[#] Python setup failed. Using existing file...\n");
            }
        } else {
            DBGPRINT("[#] User chose to skip update. Using existing file.\n");
        }
    }
    
    DWORD lastInjectedPid = 0;

    // Step 2: Find Firefox and inject
    while (true)
    {
        DWORD pid = FindProcessId(TARGET_PROC_NAME);
        if (pid != 0 && pid != lastInjectedPid)
        {
            DBGPRINT("[#] Found Firefox.exe (PID = %u)\n", pid);

            char dllFullPath[MAX_PATH];
            if (GetFullPathNameA(DLL_PATH, MAX_PATH, dllFullPath, NULL))
            {
                if (InjectDll(pid, dllFullPath))
                {
                    DBGPRINT("[#] Successfully injected %s\n", DLL_PATH);
                }
                else
                {
                    DBGPRINT("[#] Injection failed into PID %u\n", pid);
                    lastInjectedPid = pid;
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }
            }
            else
            {
                DBGPRINT("[#] Failed to get full path to %s\n", DLL_PATH);
                lastInjectedPid = pid;
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            lastInjectedPid = pid;

            HWND hwndPT = NULL;
            const int   maxAttempts   = 30;            
            const int   waitIntervalS = 2;         
            int         attempt       = 0;

            while (attempt < maxAttempts)
            {
                hwndPT = FindMainWindow(pid);
                if (hwndPT != NULL)
                    break;

                ++attempt;
                DBGPRINT("[#] Waiting for Firefox window (attempt %d/%d)...\n", attempt, maxAttempts);
                std::this_thread::sleep_for(std::chrono::seconds(waitIntervalS));
            }

            if (hwndPT == NULL)
            {
                DBGPRINT("[#] Timed out waiting for Firefox main window.\n");
                continue;
            }

            DBGPRINT("[#] Firefox window found (HWND = 0x%p)\n", hwndPT);

            // Step 3: Launch overlay
            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};
            std::string cmd = "Overlay.exe";

            if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 
                              0, NULL, NULL, &si, &pi))
            {
                DBGPRINT("[#] Launched CCNA Overlay.exe successfully.\n");
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                break; 
            }
            DWORD err = GetLastError();
            DBGPRINT("[#] Failed to launch Overlay.exe (Error %u).\n", err);
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    DBGPRINT("[#] CCNA Injector is exiting.\n");
    return 0;
}