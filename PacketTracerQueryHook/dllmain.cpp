#include <Windows.h>
#include <fstream>
#include <string>
#include <mutex>
#include <iostream>
#include "minhook/include/MinHook.h"

// Define the function prototype for QSqlQuery::exec
typedef bool (__fastcall *QSqlQuery_exec_t)(void* thisptr, const char* query);

// Original function pointer
QSqlQuery_exec_t QSqlQuery_exec_Original = nullptr;

// Global log file
std::ofstream logFile;
std::mutex logMutex;

// Log settings
bool logEmptyQueries = false;
bool logRawBytes = false;
bool logSelectQueries = true;

// Log a message to both console and file
void LogMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << message << std::endl;
    logFile << message << std::endl;
    logFile.flush();
}

// Safe check if memory is readable
int IsSafeToRead(const void* ptr, size_t size) {
    __try {
        volatile char dummy;
        const char* p = (const char*)ptr;
        for (size_t i = 0; i < size; i++) {
            dummy = p[i];
        }
        return 1;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Extract memory bytes to a buffer
void SafeExtractMemory(const void* src, char* dest, size_t maxBytes) {
    __try {
        memset(dest, 0, maxBytes);
        if (src) {
            memcpy(dest, src, maxBytes);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        strcpy_s(dest, maxBytes, "<memory access error>");
    }
}

// Properly extract a Qt QString from memory
std::string ExtractQtString(const void* qStringPtr) {
    if (!qStringPtr || !IsSafeToRead(qStringPtr, sizeof(void*))) {
        return "<null or invalid pointer>";
    }
    
    // Get the d-pointer (first pointer in the QString object)
    void* dataPtr = *(void**)qStringPtr;
    if (!dataPtr || !IsSafeToRead(dataPtr, 32)) {
        return "<invalid data pointer>";
    }
    
    // Extract the first 256 bytes of the data for analysis
    unsigned char buffer[256];
    SafeExtractMemory(dataPtr, (char*)buffer, sizeof(buffer));
    
    // Only log raw bytes if enabled
    if (logRawBytes) {
        char hexDump[128] = {0};
        sprintf_s(hexDump, "Raw length bytes: %02x %02x %02x %02x %02x %02x %02x %02x", 
                 buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
        LogMessage(hexDump);
    }
    
    // Based on observed pattern, the string length is at offset 0x04
    int stringLength = *(int*)(buffer + 4);
    
    // Sanity check
    if (stringLength < 0 || stringLength > 10000) {
        return "<invalid string length>";
    }
    
    // From the dumps, we can see the actual UTF-16 text starts at offset 0x18 (24)
    unsigned short* utf16Text = (unsigned short*)(buffer + 24);
    
    // Convert the UTF-16 string to UTF-8/ASCII
    std::string result;
    for (int i = 0; i < stringLength && i < 100; i++) {
        // Check if we're still in bounds of our buffer
        if (i * 2 + 24 >= sizeof(buffer)) break;
        
        unsigned short ch = utf16Text[i];
        if (ch < 128) {
            // ASCII character
            result.push_back((char)ch);
        } else {
            // Non-ASCII character (basic handling)
            result.push_back('?');
        }
    }
    
    return result;
}

// Check if a query is worth logging based on its content
bool ShouldLogQuery(const std::string& query) {
    // Don't log empty queries
    if (query.empty() || query == "<null>" || query == "<inaccessible memory>") {
        return logEmptyQueries;
    }
    
    // Filter out SELECT queries if they're disabled
    if (!logSelectQueries && query.find("SELECT") == 0) {
        return false;
    }
    
    return true;
}

// Our hook function
bool __fastcall QSqlQuery_exec_Hook(void* thisptr, const char* query) {
    // Try to extract the SQL query
    std::string sqlQuery = "<unknown>";
    
    // Examine the query pointer
    if (query != nullptr) {
        // Check if the query is a direct string or a QString object
        if (IsSafeToRead(query, 10)) {
            // Check if it looks like ASCII text
            bool isPlainText = true;
            for (int i = 0; i < 10 && query[i] != 0; i++) {
                if (!isprint((unsigned char)query[i]) && query[i] != '\n' && query[i] != '\r' && query[i] != '\t') {
                    isPlainText = false;
                    break;
                }
            }
            
            if (isPlainText) {
                // It's a plain C string
                sqlQuery = query;
            } else {
                // It might be a QString object, try to extract it
                sqlQuery = ExtractQtString(query);
            }
        } else {
            sqlQuery = "<inaccessible memory>";
        }
    } else {
        sqlQuery = "<null>";
    }
    
    // Call the original function
    bool result = false;
    if (QSqlQuery_exec_Original) {
        result = QSqlQuery_exec_Original(thisptr, query);
    } else {
        LogMessage("ERROR: Original function pointer is null");
        return false;
    }
    
    // Only log if this query is worth logging
    if (ShouldLogQuery(sqlQuery)) {
        // Get timestamp for logging
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timestamp[30];
        sprintf_s(timestamp, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
        
        // Format the message
        std::string status = result ? "OK" : "FAILED";
        char logMsg[2048];
        sprintf_s(logMsg, "%s[%s] %s", timestamp, status.c_str(), sqlQuery.c_str());
        LogMessage(logMsg);
    }
    
    return result;
}

// Entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        // Set up console
        AllocConsole();
        SetConsoleTitle(L"Packet Tracer SQL Monitor");
        
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        
        std::ios::sync_with_stdio();
        
        // Current time for the log
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timeStr[50];
        sprintf_s(timeStr, "%04d-%02d-%02d %02d:%02d:%02d", 
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        
        // Create log file
        logFile.open("packet_tracer_sql.log", std::ios::app);
        LogMessage("===== QueryLogger DLL Injected at " + std::string(timeStr) + " =====");
        LogMessage("User: TarekBtw");
        
        // Find Qt5Sql.dll
        HMODULE hQt5Sql = GetModuleHandle(L"Qt5Sql.dll");
        if (!hQt5Sql) {
            hQt5Sql = LoadLibrary(L"Qt5Sql.dll");
            if (!hQt5Sql) {
                LogMessage("Failed to find Qt5Sql.dll");
                return TRUE;
            }
        }
        
        // Use our original offset
        void* targetAddr = (void*)((uintptr_t)hQt5Sql + 0x2010);
        char addrMsg[100];
        sprintf_s(addrMsg, "Target address: %p", targetAddr);
        LogMessage(addrMsg);
        
        // Initialize MinHook
        if (MH_Initialize() != MH_OK) {
            LogMessage("Failed to initialize MinHook");
            return TRUE;
        }
        
        // Create the hook
        MH_STATUS status = MH_CreateHook(
            targetAddr, 
            &QSqlQuery_exec_Hook, 
            reinterpret_cast<LPVOID*>(&QSqlQuery_exec_Original)
        );
        
        if (status != MH_OK) {
            char statusMsg[100];
            sprintf_s(statusMsg, "Failed to create hook: %d", status);
            LogMessage(statusMsg);
            return TRUE;
        }
        
        // Enable the hook
        status = MH_EnableHook(targetAddr);
        if (status != MH_OK) {
            char statusMsg[100];
            sprintf_s(statusMsg, "Failed to enable hook: %d", status);
            LogMessage(statusMsg);
            return TRUE;
        }
        
        LogMessage("Hook installed successfully!");
        LogMessage("Monitoring SQL queries.");
        LogMessage("--------------------------------");
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // Disable and clean up
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        
        // Close log file
        if (logFile.is_open()) {
            LogMessage("==== QueryLogger DLL Unloaded ====");
            logFile.close();
        }
        
        // Clean up console
        FreeConsole();
    }
    return TRUE;
}