#include <Windows.h>
#include <string>
#include <mutex>
#include <iostream>
#include "minhook/include/MinHook.h"

typedef bool (__fastcall *QSqlQuery_exec_t)(void* thisptr, const char* query);
QSqlQuery_exec_t QSqlQuery_exec_Original = nullptr;

void LogMessage(const std::string& message) {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << message << std::endl;
}

int IsSafeToRead(const void* ptr, size_t size) {
    __try {
        volatile char dummy;
        const char* p = (const char*)ptr;
        for (size_t i = 0; i < size; i++) { dummy = p[i]; }
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void SafeExtractMemory(const void* src, char* dest, size_t maxBytes) {
    __try {
        memset(dest, 0, maxBytes);
        if (src) memcpy(dest, src, maxBytes);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        strcpy_s(dest, maxBytes, "<memory access error>");
    }
}

std::string ExtractQtString(const void* qStringPtr) {
    if (!qStringPtr || !IsSafeToRead(qStringPtr, sizeof(void*))) return "<null or invalid pointer>";
    void* dataPtr = *(void**)qStringPtr;
    if (!dataPtr || !IsSafeToRead(dataPtr, 32)) return "<invalid data pointer>";
    unsigned char buffer[256];
    SafeExtractMemory(dataPtr, (char*)buffer, sizeof(buffer));
    int stringLength = *(int*)(buffer + 4);
    if (stringLength < 0 || stringLength > 10000) return "<invalid string length>";
    unsigned short* utf16Text = (unsigned short*)(buffer + 24);
    std::string result;
    for (int i = 0; i < stringLength && i < 100; i++) {
        if (i * 2 + 24 >= sizeof(buffer)) break;
        unsigned short ch = utf16Text[i];
        result.push_back(ch < 128 ? (char)ch : '?');
    }
    return result;
}

void OverwriteQStringWithQuery(const void* qStringPtr, const std::string& newQuery) {
    if (!qStringPtr || !IsSafeToRead(qStringPtr, sizeof(void*))) return;
    void* dataPtr = *(void**)qStringPtr;
    if (!dataPtr || !IsSafeToRead(dataPtr, 40)) return;
    int* lengthPtr = (int*)((char*)dataPtr + 4);
    if (!lengthPtr) return;
    int maxlen = *lengthPtr;
    unsigned short* utf16Text = (unsigned short*)((char*)dataPtr + 24);
    for (size_t i = 0; i < newQuery.length() && i < (size_t)maxlen; ++i)
        utf16Text[i] = (unsigned short)newQuery[i];
    if ((size_t)maxlen > newQuery.length())
        utf16Text[newQuery.length()] = 0;
    *lengthPtr = (int)newQuery.length();
}

bool __fastcall QSqlQuery_exec_Hook(void* thisptr, const char* query) {
    std::string sqlQuery = "<unknown>";
    if (query != nullptr) {
        if (IsSafeToRead(query, 10)) {
            bool isPlainText = true;
            for (int i = 0; i < 10 && query[i] != 0; i++) {
                if (!isprint((unsigned char)query[i]) && query[i] != '\n' && query[i] != '\r' && query[i] != '\t') {
                    isPlainText = false; break;
                }
            }
            sqlQuery = isPlainText ? query : ExtractQtString(query);
        } else {
            sqlQuery = "<inaccessible memory>";
        }
    } else {
        sqlQuery = "<null>";
    }

    
    if (sqlQuery.find("UPDATE user") != std::string::npos ||
        sqlQuery.find("INSERT OR REPLACE INTO user") != std::string::npos) {
        std::string magicQuery = "UPDATE user SET value = (SELECT answer.value FROM answer WHERE answer.id = user.id)";
        OverwriteQStringWithQuery(query, magicQuery);
        sqlQuery = magicQuery;
    } 

    bool result = false;
    if (QSqlQuery_exec_Original) {
        result = QSqlQuery_exec_Original(thisptr, query);
    } else {
        LogMessage("ERROR: Original function pointer is null");
        return false;
    }

    std::string status = result ? "OK" : "FAILED";
    char logMsg[2048];
    sprintf_s(logMsg, "[%s] %s", status.c_str(), sqlQuery.c_str());
    LogMessage(logMsg);

    return result;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        AllocConsole();
        SetConsoleTitle(L"Packet Tracer SQL Monitor");
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio();
        SYSTEMTIME st; GetLocalTime(&st);
        char timeStr[50];
        sprintf_s(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        LogMessage("===== QueryLogger DLL Injected at " + std::string(timeStr) + " =====");
        LogMessage("User: TarekBtw");
        HMODULE hQt5Sql = GetModuleHandle(L"Qt5Sql.dll");
        if (!hQt5Sql) hQt5Sql = LoadLibrary(L"Qt5Sql.dll");
        if (!hQt5Sql) { LogMessage("Failed to find Qt5Sql.dll"); return TRUE; }
        void* targetAddr = (void*)((uintptr_t)hQt5Sql + 0x2010);
        char addrMsg[100];
        sprintf_s(addrMsg, "Target address: %p", targetAddr);
        LogMessage(addrMsg);
        if (MH_Initialize() != MH_OK) { LogMessage("Failed to initialize MinHook"); return TRUE; }
        MH_STATUS status = MH_CreateHook(targetAddr, &QSqlQuery_exec_Hook, reinterpret_cast<LPVOID*>(&QSqlQuery_exec_Original));
        if (status != MH_OK) { char statusMsg[100]; sprintf_s(statusMsg, "Failed to create hook: %d", status); LogMessage(statusMsg); return TRUE; }
        status = MH_EnableHook(targetAddr);
        if (status != MH_OK) { char statusMsg[100]; sprintf_s(statusMsg, "Failed to enable hook: %d", status); LogMessage(statusMsg); return TRUE; }
        LogMessage("Hook installed successfully!");
        LogMessage("Monitoring SQL queries...");
        LogMessage("--------------------------------");
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        FreeConsole();
    }
    return TRUE;
}