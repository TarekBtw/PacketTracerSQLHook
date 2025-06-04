#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

const wchar_t* TARGET_PROC_NAME = L"PacketTracer.exe";
const char*    DLL_PATH         = "PacketTracerQueryHook.dll";

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
    if (!hProc) { std::cout << "OpenProcess failed\n"; return false; }
    LPVOID mem = VirtualAllocEx(hProc, nullptr, dllPath.size() + 1, MEM_COMMIT, PAGE_READWRITE);
    if (!mem) { std::cout << "VirtualAllocEx failed\n"; CloseHandle(hProc); return false; }
    WriteProcessMemory(hProc, mem, dllPath.c_str(), dllPath.size() + 1, nullptr);
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
        (LPTHREAD_START_ROUTINE)LoadLibraryA, mem, 0, nullptr);
    if (!hThread) { std::cout << "CreateRemoteThread failed\n"; }
    else { std::cout << "Injected DLL!\n"; WaitForSingleObject(hThread, INFINITE); CloseHandle(hThread); }
    VirtualFreeEx(hProc, mem, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return true;
}

int main()
{
    DWORD lastInjectedPid = 0;

    while (true)
    {
        DWORD pid = FindProcessId(TARGET_PROC_NAME);
        if (pid != 0 && pid != lastInjectedPid)
        {
            std::cout << "[+] Found PacketTracer.exe (PID = " << pid << ")\n";

            char dllFullPath[MAX_PATH];
            if (GetFullPathNameA(DLL_PATH, MAX_PATH, dllFullPath, NULL))
            {
                if (InjectDll(pid, dllFullPath))
                {
                    std::cout << "[+] Successfully injected " << DLL_PATH << "\n";
                }
                else
                {
                    std::cout << "[-] Injection failed into PID " << pid << "\n";
                    lastInjectedPid = pid;
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }
            }
            else
            {
                std::cout << "[-] Failed to get full path to " << DLL_PATH << "\n";
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
                std::cout << "[…] Waiting for PacketTracer window (attempt "
                          << attempt << "/" << maxAttempts << ")…\n";
                std::this_thread::sleep_for(std::chrono::seconds(waitIntervalS));
            }

            if (hwndPT == NULL)
            {
                std::cout << "[-] Timed out waiting for PacketTracer’s main window.\n";
                continue;
            }

            std::cout << "[+] PacketTracer window found (HWND = 0x"
                      << std::hex << (intptr_t)hwndPT << std::dec << ")\n";

            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};
            std::string cmd = "Overlay.exe";

            if (CreateProcessA(
                    NULL,
                    (LPSTR)cmd.c_str(),
                    NULL,
                    NULL,
                    FALSE,
                    0,
                    NULL,
                    NULL,
                    &si,
                    &pi))
            {
                std::cout << "[+] Launched Overlay.exe successfully.\n";
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                break; 
            }
                DWORD err = GetLastError();
                std::cout << "[-] Failed to launch Overlay.exe (Error " << err << ").\n";
                break;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    std::cout << "[*] Injector is exiting.\n";
    return 0;
}