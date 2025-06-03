#include <algorithm>
#include <Windows.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <dxgi.h>
#include "device_overlay.h"
#include "minhook/include/MinHook.h"

#undef max



typedef bool (__fastcall *QSqlQuery_exec_t)(void* thisptr, const char* query);
QSqlQuery_exec_t QSqlQuery_exec_Original = nullptr;

std::map<std::string, std::map<std::string, std::string>>* g_deviceAnswers = nullptr;
std::atomic<bool> g_running(true);

// --- Memory safety helpers ---
extern "C" int SafeMemCopy(void* dest, const void* src, size_t size) {
    __try { memcpy(dest, src, size); return 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" int IsSafeToRead(const void* ptr, size_t size) {
    __try {
        volatile char dummy;
        const char* p = (const char*)ptr;
        for (size_t i = 0; i < size; i++) { dummy = p[i]; }
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
std::string ExtractQtString(const void* qStringPtr) {
    if (!qStringPtr || !IsSafeToRead(qStringPtr, sizeof(void*))) return "<null or invalid pointer>";
    void* dataPtr = *(void**)qStringPtr;
    if (!dataPtr || !IsSafeToRead(dataPtr, 32)) return "<invalid data pointer>";
    unsigned char buffer[1024] = {};
    if (!SafeMemCopy(buffer, dataPtr, sizeof(buffer))) return "<memory access error>";
    int stringLength = *(int*)(buffer + 4);
    if (stringLength < 0 || stringLength > 500) stringLength = 500;
    unsigned short* utf16Text = (unsigned short*)(buffer + 24);
    std::string result;
    for (int i = 0; i < stringLength && i < 500; i++) {
        if (i * 2 + 24 >= sizeof(buffer)) break;
        unsigned short ch = utf16Text[i];
        result.push_back(ch < 128 ? (char)ch : '?');
    }
    return result;
}

// --- SQL answer parsing ---
bool ParseAnswerRow(const std::string& sql, std::string& device, std::string& field, std::string& value) {
    size_t v = sql.find("VALUES(");
    if (v == std::string::npos) return false;
    std::string vals = sql.substr(v + 7);
    size_t end = vals.find(")");
    if (end != std::string::npos) vals = vals.substr(0, end);

    std::vector<std::string> fields;
    std::string cur;
    bool dq = false;
    for (char c : vals) {
        if (c == '"') dq = !dq;
        if (c == ',' && !dq) { fields.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) fields.push_back(cur);

    if (fields.size() < 4) return false;
    device = fields[1];
    field  = fields[2];
    value  = fields[3];
    return true;
}

// --- Table clear on grading event ---
void MaybeClearOnAnswerTable(const std::string& sql) {
    if (!g_deviceAnswers) return;
    if (
        (sql.find("DROP TABLE") != std::string::npos && sql.find("answer") != std::string::npos) ||
        (sql.find("CREATE TABLE") != std::string::npos && sql.find("answer") != std::string::npos)
    ) {
        g_deviceAnswers->clear();
    }
}

void PrintCleanDeviceAnswers() {
    if (!g_deviceAnswers || g_deviceAnswers->empty()) return;
    std::cout << "\n======= Packet Tracer Solution Dump =======\n";
    // Gather devices sorted
    std::vector<std::string> deviceNames;
    for (const auto& kv : *g_deviceAnswers) deviceNames.push_back(kv.first);
    std::sort(deviceNames.begin(), deviceNames.end());
    for (const auto& device : deviceNames) {
        std::cout << "\nDevice: " << device << "\n";
        // Gather pairs sorted by field/node name
        std::vector<std::pair<std::string, std::string>> fields(
            g_deviceAnswers->at(device).begin(),
            g_deviceAnswers->at(device).end()
        );
        std::sort(fields.begin(), fields.end(),
            [](const auto& a, const auto& b){ return a.first < b.first; });
        for (const auto& kv : fields) {
            std::cout << "- \"" << kv.first << "\" : \"" << kv.second << "\"\n";
        }
    }
    std::cout << "===========================================\n";
}

// --- Hotkey thread ---
DWORD WINAPI HotkeyThread(LPVOID) {
    // F9 key (VK_F9) - change if you want another key
    std::cout << "Press F9 to print the solution table!" << std::endl;
    while (g_running) {
        if (GetAsyncKeyState(VK_F9) & 1) { // on press
            PrintCleanDeviceAnswers();
        }
        Sleep(50);
    }
    return 0;
}

// --- Main hook ---
bool __fastcall QSqlQuery_exec_Hook(void* thisptr, const char* query) {
    std::string sqlQuery = "<unknown>";
    if (query != nullptr) {
        bool isPlainText = true;
        for (int i = 0; i < 10 && query[i] != 0; i++) {
            if (!isprint((unsigned char)query[i]) && query[i] != '\n' && query[i] != '\r' && query[i] != '\t') {
                isPlainText = false; break;
            }
        }
        if (isPlainText) {
            sqlQuery = query;
        } else {
            sqlQuery = ExtractQtString(query);
        }
    }

    MaybeClearOnAnswerTable(sqlQuery);

    if (g_deviceAnswers && sqlQuery.find("INSERT OR REPLACE INTO answer") != std::string::npos) {
        std::string device, field, value;
        if (ParseAnswerRow(sqlQuery, device, field, value)) {
            (*g_deviceAnswers)[device][field] = value;
            // Do NOT print here: only print on keybind or detach
        }
    }

    if (QSqlQuery_exec_Original)
        return QSqlQuery_exec_Original(thisptr, query);
    else
        return false;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    static HANDLE hThread = nullptr;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        AllocConsole();
        SetConsoleTitle(L"Packet Tracer Device Solution Table");
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio();
        std::cout << "DLL injected!" << std::endl;
        g_deviceAnswers = new std::map<std::string, std::map<std::string, std::string>>();
        HMODULE hQt5Sql = GetModuleHandle(L"Qt5Sql.dll");
        if (!hQt5Sql) hQt5Sql = LoadLibrary(L"Qt5Sql.dll");
        void* targetAddr = (void*)((uintptr_t)hQt5Sql + 0x2010); // <-- verify this offset for your Qt5Sql.dll!
        MH_Initialize();
        MH_CreateHook(targetAddr, &QSqlQuery_exec_Hook, reinterpret_cast<LPVOID*>(&QSqlQuery_exec_Original));
        MH_EnableHook(targetAddr);

        // Start hotkey thread
        g_running = true;
        hThread = CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);

        // Start D3D11 overlay hook thread
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);

    } else if (reason == DLL_PROCESS_DETACH) {
        g_running = false;
        if (hThread) {
            WaitForSingleObject(hThread, 1000);
            CloseHandle(hThread);
            hThread = nullptr;
        }
        if (g_deviceAnswers) { delete g_deviceAnswers; g_deviceAnswers = nullptr; }
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        FreeConsole();
    }
    return TRUE;
}