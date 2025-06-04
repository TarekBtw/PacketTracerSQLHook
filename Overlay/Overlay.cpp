#include <Windows.h>
#undef max
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <streambuf>
#include <atomic>
#include <sstream>
#include <windowsx.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_internal.h"

#pragma comment(lib, "d3d11.lib")

static HWND g_TargetHwnd = nullptr;
static HWND g_OverlayHwnd = nullptr;

std::string overlayText;                 
std::atomic<LONGLONG> lastFileTime = 0;
std::string filePath;

ImFont* tab_text1 = nullptr;
ImFont* tab_text2 = nullptr;
ImFont* ico = nullptr;
ImFont* ico_subtab = nullptr;
float dpi_scale = 1.0f;

std::vector<std::string> deviceSections;
int currentDeviceIndex = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

std::string GetCurrentDir() {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string fullPath(path);
    size_t pos = fullPath.find_last_of("\\/");
    if (pos != std::string::npos) {
        return fullPath.substr(0, pos + 1);
    }
    return "";
}

void ParseDeviceSections() {
    deviceSections.clear();
    if (overlayText.empty()) return;

    std::istringstream iss(overlayText);
    std::string line;
    std::string currentSection;
    bool firstSectionFound = false;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.rfind("Device: ", 0) == 0) {
            if (firstSectionFound) {
                deviceSections.push_back(currentSection);
                currentSection.clear();
            } else {
                firstSectionFound = true;
            }
        }

        if (firstSectionFound) {
            currentSection += line;
            currentSection += "\n";
        }
    }
    if (firstSectionFound && !currentSection.empty()) {
        deviceSections.push_back(currentSection);
    }
}

void LoadOverlayText() {
    if (filePath.empty()) {
        char tempPathBuf[MAX_PATH] = {0};
        DWORD len = GetTempPathA(MAX_PATH, tempPathBuf);
        if (len > 0 && len < MAX_PATH) {
            filePath = std::string(tempPathBuf) + "devices.txt";
        } else {
            filePath = GetCurrentDir() + "devices.txt";
        }
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        LONGLONG ft = (((LONGLONG)fad.ftLastWriteTime.dwHighDateTime) << 32)
                      | fad.ftLastWriteTime.dwLowDateTime;
        if (ft != lastFileTime) {
            lastFileTime = ft;
            std::ifstream t(filePath);
            overlayText.assign(
                (std::istreambuf_iterator<char>(t)),
                std::istreambuf_iterator<char>()
            );
            ParseDeviceSections();    
            if (!deviceSections.empty() &&
                currentDeviceIndex >= (int)deviceSections.size())
            {
                currentDeviceIndex = 0;
            }
        }
    }
}

#define DBGPRINT(fmt, ...) do { \
    char dbgprint_buf[512]; \
    snprintf(dbgprint_buf, sizeof(dbgprint_buf), fmt, __VA_ARGS__); \
    OutputDebugStringA(dbgprint_buf); \
} while(0)

HWND FindPacketTracerWindow() {
    HWND result = nullptr;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        char title[256] = {};
        GetWindowTextA(hwnd, title, sizeof(title));
        if (strstr(title, "Cisco Packet Tracer")) {
            *((HWND*)lParam) = hwnd;
            return FALSE; 
        }
        return TRUE; 
    }, (LPARAM)&result);
    return result;
}
static const int RESIZE_BORDER = 6;
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST: {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hWnd, &pt);

            RECT rc;
            GetClientRect(hWnd, &rc);
            int w = rc.right;
            int h = rc.bottom;

            bool left   = (pt.x <  RESIZE_BORDER);
            bool right  = (pt.x >= w - RESIZE_BORDER);
            bool top    = (pt.y <  RESIZE_BORDER);
            bool bottom = (pt.y >= h - RESIZE_BORDER);

            if (left && top)      return HTTOPLEFT;
            if (right && top)     return HTTOPRIGHT;
            if (left && bottom)   return HTBOTTOMLEFT;
            if (right && bottom)  return HTBOTTOMRIGHT;
            if (left)             return HTLEFT;
            if (right)            return HTRIGHT;
            if (top)              return HTTOP;
            if (bottom)           return HTBOTTOM;
            return HTCAPTION;
    }
    default:
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
    DBGPRINT("Overlay: WinMain started\n");

    RegisterHotKey(NULL, 1, 0, VK_INSERT);

    HWND targetHwnd = FindPacketTracerWindow();
    if (!targetHwnd) {
        DBGPRINT("Overlay: Could not find Packet Tracer window, exiting.\n");
        return 1;
    }
    g_TargetHwnd = targetHwnd;
    DBGPRINT("Overlay: Found Packet Tracer window, HWND=0x%p\n", targetHwnd);

    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX),
        CS_CLASSDC,
        WndProc,
        0L, 0L,
        GetModuleHandle(NULL),
        NULL, NULL, NULL, NULL,
        _T("PacketTracerOverlay"),
        NULL
    };
    if (!RegisterClassEx(&wc)) {
        DBGPRINT("Overlay: RegisterClassEx failed!\n");
        return 1;
    }
    DBGPRINT("Overlay: Window class registered\n");

    RECT rc;
    if (!GetWindowRect(targetHwnd, &rc)) {
        DBGPRINT("Overlay: GetWindowRect failed!\n");
        return 1;
    }
    DBGPRINT("Overlay: PT window rect: L=%ld T=%ld R=%ld B=%ld\n",
             rc.left, rc.top, rc.right, rc.bottom);

    int overlayWidth  = 1000;
    int overlayHeight = 300;
    int x = rc.right - overlayWidth - 32;
    int y = rc.bottom - overlayHeight - 64;

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        wc.lpszClassName, _T("Packet Tracer Solution Overlay"),
        WS_POPUP,
        x, y, overlayWidth, overlayHeight,
        NULL, NULL, wc.hInstance, NULL
    );
    SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 0, LWA_COLORKEY);
    
    DBGPRINT("Overlay: CreateWindowEx returned hwnd=0x%p\n", hwnd);
    g_OverlayHwnd = hwnd;
    if (!hwnd) {
        DBGPRINT("Overlay: CreateWindowEx failed!\n");
        return 1;
    }

    DBGPRINT("Overlay: SetLayeredWindowAttributes set\n");

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width  = overlayWidth;
    sd.BufferDesc.Height = overlayHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow     = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed         = TRUE;
    sd.SwapEffect       = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* device         = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain     = nullptr;
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &swapChain, &device, &featureLevel, &context
    );
    DBGPRINT("Overlay: D3D11CreateDeviceAndSwapChain = 0x%08lx\n", hr);
    if (FAILED(hr)) {
        DBGPRINT("Overlay: D3D11CreateDeviceAndSwapChain failed!\n");
        return 1;
    }

    ID3D11RenderTargetView* mainRenderTargetView = nullptr;
    {
        ID3D11Texture2D* pBackBuffer = nullptr;
        HRESULT hr2 = swapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        DBGPRINT("Overlay: swapChain->GetBuffer = 0x%08lx\n", hr2);
        if (FAILED(hr2)) {
            DBGPRINT("Overlay: swapChain->GetBuffer failed!\n");
            return 1;
        }
        HRESULT hr3 = device->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
        DBGPRINT("Overlay: device->CreateRenderTargetView = 0x%08lx\n", hr3);
        if (FAILED(hr3)) {
            DBGPRINT("Overlay: CreateRenderTargetView failed!\n");
            return 1;
        }
        pBackBuffer->Release();
    }

    ImGui::CreateContext();
    DBGPRINT("Overlay: ImGui context created\n");
    ImGui_ImplWin32_Init(hwnd);
    DBGPRINT("Overlay: ImGui_ImplWin32_Init done\n");
    ImGui_ImplDX11_Init(device, context);
    DBGPRINT("Overlay: ImGui_ImplDX11_Init done\n");

    ShowWindow(hwnd, SW_SHOW);
    DBGPRINT("Overlay: ShowWindow done\n");

    bool overlayVisible = true;
    static ImVec2 overlayPos = ImVec2(0, 0);
    static float overlayScale = 1.0f;

    int lastSwapWidth  = overlayWidth;
    int lastSwapHeight = overlayHeight;

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    bool running = true;
    int frame = 0;

    while (running) {
        int desiredW = 1000;
        int desiredH = 300;
        RECT hwndRect;
        GetClientRect(g_OverlayHwnd, &hwndRect);
        int currW = hwndRect.right - hwndRect.left;
        int currH = hwndRect.bottom - hwndRect.top;
        if (currW != desiredW || currH != desiredH) {
            SetWindowPos(g_OverlayHwnd, NULL, 0, 0, desiredW, desiredH, SWP_NOZORDER | SWP_NOMOVE);
            swapChain->ResizeBuffers(0, desiredW, desiredH, DXGI_FORMAT_UNKNOWN, 0);
            if (mainRenderTargetView) {
                mainRenderTargetView->Release();
                mainRenderTargetView = nullptr;
            }
            ID3D11Texture2D* pNewBackBuffer = nullptr;
            if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&pNewBackBuffer)))) {
                device->CreateRenderTargetView(pNewBackBuffer, NULL, &mainRenderTargetView);
                pNewBackBuffer->Release();
            }
            lastSwapWidth = desiredW;
            lastSwapHeight = desiredH;
        }

        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && msg.wParam == 1) {
                overlayVisible = !overlayVisible;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
            }
        }

        if (!IsWindow(targetHwnd)) {
            break;
        }

        if (overlayVisible) {
            ShowWindow(hwnd, SW_SHOW);
        } else {
            ShowWindow(hwnd, SW_HIDE);
        }

        LONG exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW;
        if (!overlayVisible) {
            SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
        } else {
            SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
        }

        if (frame % 30 == 0) {
            LoadOverlayText();
        }
        ++frame;

        if (!deviceSections.empty()) {
            if (GetAsyncKeyState(VK_PRIOR) & 1) {
                currentDeviceIndex = (currentDeviceIndex - 1 + (int)deviceSections.size()) % (int)deviceSections.size();
            }
            if (GetAsyncKeyState(VK_NEXT) & 1) {
                currentDeviceIndex = (currentDeviceIndex + 1) % (int)deviceSections.size();
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        io.FontGlobalScale = overlayScale;
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            overlayScale += io.MouseWheel * 0.1f;
            overlayScale = ImClamp(overlayScale, 0.3f, 3.0f);
        }

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0.20f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg,       ImVec4(1,1,1,1.00f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(1,1,1,1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,0.33f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4,4));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(2,2));

        const char* bodyText = "(no device data)";
        if (!deviceSections.empty()) {
            bodyText = deviceSections[currentDeviceIndex].c_str();
        }

        std::string footerText;
        if (!deviceSections.empty()) {
            std::stringstream ss;
            ss << "Device " << (currentDeviceIndex + 1) << "/" << deviceSections.size();
            footerText = ss.str();
        }

        ImGuiWindowFlags window_flags =
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoBackground;

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1000, 300), ImGuiCond_Always);
        ImGui::Begin("##DeviceSolution", nullptr, window_flags);

        ImGui::BeginChild("BodyScroll", ImVec2(580, 330), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(bodyText);
        ImGui::EndChild();

        if (!footerText.empty()) {
            ImGui::TextDisabled(footerText.c_str());
        }
        ImGui::End();

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        ImGui::EndFrame();
        ImGui::Render();

        const float clear_col[4] = {0, 0, 0, 0};
        context->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
        context->ClearRenderTargetView(mainRenderTargetView, clear_col);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain->Present(1, 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    UnregisterHotKey(NULL, 1);
    DBGPRINT("Overlay: cleaning up\n");
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (mainRenderTargetView) mainRenderTargetView->Release();
    if (swapChain) swapChain->Release();
    if (context) context->Release();
    if (device) device->Release();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    DBGPRINT("Overlay: exited WinMain\n");
    return 0;
}