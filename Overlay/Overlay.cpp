// overlay.cpp
#include <Windows.h>
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
        return fullPath.substr(0, pos + 1); // include trailing slash
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
            return FALSE; // stop enumeration
        }
        return TRUE; // keep looking
    }, (LPARAM)&result);
    return result;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        return HTCAPTION;
    }
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return DefWindowProc(hWnd, msg, wParam, lParam);
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

    int overlayWidth  = 300;
    int overlayHeight = 200;
    int x = rc.right - overlayWidth - 32;
    int y = rc.bottom - overlayHeight - 64;

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        wc.lpszClassName, _T("Packet Tracer Solution Overlay"),
        WS_OVERLAPPEDWINDOW,
        x, y, overlayWidth, overlayHeight,
        NULL, NULL, wc.hInstance, NULL
    );
    
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    SetWindowLong(hwnd, GWL_STYLE, style);
    
    DBGPRINT("Overlay: CreateWindowEx returned hwnd=0x%p\n", hwnd);
    g_OverlayHwnd = hwnd;
    if (!hwnd) {
        DBGPRINT("Overlay: CreateWindowEx failed!\n");
        return 1;
    }

    SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 0, LWA_COLORKEY);
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
    static ImVec2 overlayPos = ImVec2(10, 10);
    static float overlayScale = 1.0f;

    int lastSwapWidth  = overlayWidth;
    int lastSwapHeight = overlayHeight;

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    bool running = true;
    int frame = 0;

    while (running) {
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && msg.wParam == 1) {
                overlayVisible = !overlayVisible;
                DBGPRINT("Overlay: Insert pressed, overlayVisible = %d\n", (int)overlayVisible);
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                DBGPRINT("Overlay: got WM_QUIT, exiting\n");
                running = false;
            }
        }

        if (!IsWindow(targetHwnd)) {
            DBGPRINT("Overlay: target window destroyed, exiting\n");
            break;
        }

        // Show or hide overlay
        if (overlayVisible) {
            ShowWindow(hwnd, SW_SHOW);
        } else {
            ShowWindow(hwnd, SW_HIDE);
        }

        LONG exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW;
        if (overlayVisible) {
            SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
        } else {
            SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
        }

        if (frame % 30 == 0) {
            DBGPRINT("Overlay: LoadOverlayText at frame %d\n", frame);
            LoadOverlayText();
        }
        ++frame;

        if (!deviceSections.empty()) {
            if (GetAsyncKeyState(VK_PRIOR) & 1) { // Page Up
                currentDeviceIndex = (currentDeviceIndex - 1 + (int)deviceSections.size()) % (int)deviceSections.size();
            }
            if (GetAsyncKeyState(VK_NEXT) & 1) {  // Page Down
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

        ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(1.0f, 1.0f, 1.0f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg,       ImVec4(1.0f, 1.0f, 1.0f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(1.0f, 1.0f, 1.0f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.0f, 0.0f, 0.0f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(2.0f, 2.0f));

        ImGuiWindowFlags window_flags =
            0 |  
            ImGuiWindowFlags_NoCollapse;        

        ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("##DeviceSolution", nullptr, window_flags);

        if (!deviceSections.empty()) {
            ImGui::TextUnformatted(deviceSections[currentDeviceIndex].c_str());
        } else {
            ImGui::TextUnformatted("(no device data)");
        }

        if (!deviceSections.empty()) {
            std::stringstream footer;
            footer << "Device " << (currentDeviceIndex + 1) << "/" << deviceSections.size();
            ImGui::TextDisabled(footer.str().c_str());
        }
        ImGui::End();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        {
            ImVec2 neededSize = ImGui::GetWindowSize(); 
            int newWidth  = (int)neededSize.x;
            int newHeight = (int)neededSize.y;

            if (newWidth != lastSwapWidth || newHeight != lastSwapHeight) {
                SetWindowPos(
                    g_OverlayHwnd,
                    NULL,
                    0,  
                    0,  
                    newWidth,
                    newHeight,
                    SWP_NOZORDER | SWP_NOMOVE
                );

                if (mainRenderTargetView) {
                    mainRenderTargetView->Release();
                    mainRenderTargetView = nullptr;
                }

                swapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);

                {
                    ID3D11Texture2D* pNewBackBuffer = nullptr;
                    HRESULT hr4 = swapChain->GetBuffer(0, IID_PPV_ARGS(&pNewBackBuffer));
                    DBGPRINT("Overlay: swapChain->GetBuffer (after Resize) = 0x%08lx\n", hr4);
                    if (SUCCEEDED(hr4)) {
                        device->CreateRenderTargetView(pNewBackBuffer, NULL, &mainRenderTargetView);
                        pNewBackBuffer->Release();
                    }
                }

                lastSwapWidth  = newWidth;
                lastSwapHeight = newHeight;
                DBGPRINT("Overlay: Resized swapChain to %d×%d\n", newWidth, newHeight);
            }
        }

        overlayPos = ImGui::GetWindowPos();

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