#include <Windows.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <sstream>
#include <fstream>

#include "minhook/include/MinHook.h"


typedef bool (__fastcall* QSqlQuery_exec_t)(void* thisptr, const char* query);
static QSqlQuery_exec_t QSqlQuery_exec_Original = nullptr;

std::map<std::string, std::map<std::string, std::string>>* g_deviceAnswers = nullptr;
static std::atomic<bool> g_running(true);


extern "C" int SafeMemCopy(void* dest, const void* src, size_t size) {
    __try {
        memcpy(dest, src, size);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

std::string GetCurrentDir() {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, path, MAX_PATH); // NULL = current process exe
    std::string fullPath(path);
    size_t pos = fullPath.find_last_of("\\/");
    if (pos != std::string::npos) {
        return fullPath.substr(0, pos + 1); // include trailing slash
    }
    return "";
}

extern "C" int IsSafeToRead(const void* ptr, size_t size) {
    __try {
        volatile char dummy;
        const char* p = (const char*)ptr;
        for (size_t i = 0; i < size; i++) { dummy = p[i]; }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
static std::string ExtractQtString(const void* qStringPtr) {
    if (!qStringPtr || !IsSafeToRead(qStringPtr, sizeof(void*)))
        return "<null or invalid pointer>";

    void* dataPtr = *(void**)qStringPtr;
    if (!dataPtr || !IsSafeToRead(dataPtr, 32))
        return "<invalid data pointer>";

    unsigned char buffer[1024] = {};
    if (!SafeMemCopy(buffer, dataPtr, sizeof(buffer)))
        return "<memory access error>";

    int stringLength = *(int*)(buffer + 4);
    if (stringLength < 0 || stringLength > 500)
        stringLength = 500;

    unsigned short* utf16Text = (unsigned short*)(buffer + 24);
    std::string result;
    for (int i = 0; i < stringLength && i < 500; i++) {
        unsigned short ch = utf16Text[i];
        result.push_back(ch < 128 ? (char)ch : '?');
    }
    return result;
}


static bool ParseAnswerRow(
    const std::string& sql,
    std::string& device,
    std::string& field,
    std::string& value)
{
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
        if (c == ',' && !dq) {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) fields.push_back(cur);
    if (fields.size() < 4) return false;

    device = fields[1];
    field  = fields[2];
    value  = fields[3];
    return true;
}


static void MaybeClearOnAnswerTable(const std::string& sql) {
    if (!g_deviceAnswers) return;
    if ((sql.find("DROP TABLE") != std::string::npos && sql.find("answer") != std::string::npos) ||
        (sql.find("CREATE TABLE") != std::string::npos && sql.find("answer") != std::string::npos))
    {
        g_deviceAnswers->clear();
    }
}


static void PrintCleanDeviceAnswers() {
    if (!g_deviceAnswers || g_deviceAnswers->empty()) return;
    std::ostringstream out;
    out << "\n======= Packet Tracer Solution Dump =======\n";

    std::vector<std::string> deviceNames;
    for (const auto& kv : *g_deviceAnswers)
        deviceNames.push_back(kv.first);
    std::sort(deviceNames.begin(), deviceNames.end());

    for (const auto& device : deviceNames) {
        out << "\nDevice: " << device << "\n";
        const auto& fields = (*g_deviceAnswers)[device];
        std::vector<std::pair<std::string,std::string>> all(fields.begin(), fields.end());
        std::sort(all.begin(), all.end(),
                  [](auto& a, auto& b){ return a.first < b.first; });
        for (const auto& kv : all) {
            out << "- \"" << kv.first << "\" : \"" << kv.second << "\"\n";
        }
    }
    out << "===========================================\n";
    std::cout << out.str();

    char tempPathBuf[MAX_PATH] = {0};
    DWORD len = GetTempPathA(MAX_PATH, tempPathBuf);
    if (len > 0 && len < MAX_PATH) {
        std::string tempPath(tempPathBuf);
        std::ofstream file(tempPath + "devices.txt");
        file << out.str();
        file.close();
    } else {
        std::string dir = GetCurrentDir();
        std::ofstream file(dir + "devices.txt");
        file << out.str();
        file.close();
    }
}


static DWORD WINAPI HotkeyThread(LPVOID)
{
    std::cout << "Press F9 to print the solution table!\n";
    while (g_running) {
        if (GetAsyncKeyState(VK_F9) & 1) {
            PrintCleanDeviceAnswers();
        }
        Sleep(50);
    }
    return 0;
}

static bool __fastcall QSqlQuery_exec_Hook(void* thisptr, const char* query)
{
    std::string sqlQuery = "<unknown>";
    if (query != nullptr) {
        bool isPlainText = true;
        for (int i = 0; i < 10 && query[i] != 0; i++) {
            if (!isprint((unsigned char)query[i]) &&
                query[i] != '\n' && query[i] != '\r' && query[i] != '\t')
            {
                isPlainText = false;
                break;
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
        }
    }

    if (QSqlQuery_exec_Original) {
        return QSqlQuery_exec_Original(thisptr, query);
    } else {
        return false;
    }
}

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD   ul_reason_for_call,
    LPVOID  lpReserved)
{
    static HANDLE hHotkeyThread = nullptr;

    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        std::ios::sync_with_stdio();
        std::cout << "DLL injected!\n";

        g_deviceAnswers = new std::map<std::string, std::map<std::string, std::string>>();
        
        HMODULE hQt5Sql = GetModuleHandle(L"Qt5Sql.dll");
        if (!hQt5Sql) {
            hQt5Sql = LoadLibrary(L"Qt5Sql.dll");
        }
        if (hQt5Sql) {
            void* targetAddr = (void*)((uintptr_t)hQt5Sql + 0x2010);
            MH_Initialize();
            if (MH_CreateHook(targetAddr,
                              &QSqlQuery_exec_Hook,
                              reinterpret_cast<LPVOID*>(&QSqlQuery_exec_Original)) == MH_OK)
            {
                MH_EnableHook(targetAddr);
                std::cout << "[DEBUG] Hooked QSqlQuery::exec successfully.\n";
            } else {
                std::cout << "[ERROR] Failed to create hook for QSqlQuery::exec!\n";
            }
        } else {
            std::cout << "[ERROR] Failed to load Qt5Sql.dll!\n";
        }

        g_running = true;
        hHotkeyThread = CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
    }

    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        g_running = false;
        if (hHotkeyThread) {
            WaitForSingleObject(hHotkeyThread, 500);
            CloseHandle(hHotkeyThread);
            hHotkeyThread = nullptr;
        }

        if (g_deviceAnswers) {
            delete g_deviceAnswers;
            g_deviceAnswers = nullptr;
        }

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        FreeConsole();
    }
    return TRUE;
}