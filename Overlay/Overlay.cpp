#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#undef max
#undef min
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <algorithm>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_internal.h"

#pragma comment(lib, "d3d11.lib")

// Fix for unresolved externals
ImFont* tab_text1 = nullptr;
ImFont* tab_text2 = nullptr;
ImFont* ico = nullptr;
ImFont* ico_subtab = nullptr;
float dpi_scale = 1.0f;

static HWND g_OverlayHwnd = nullptr;

#define DBGPRINT(fmt, ...) do { \
char dbgprint_buf[512]; \
snprintf(dbgprint_buf, sizeof(dbgprint_buf), fmt, __VA_ARGS__); \
OutputDebugStringA(dbgprint_buf); \
} while(0)

struct CCNAQuestion {
    int id;
    std::string question;
    std::vector<std::string> correctAnswers;
    std::string sourceUrl;
};

// Global style override function
void SetOverlayStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    
    // Set ALL colors at once to avoid inheritance issues
    colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 0.33f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.20f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.1f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
    colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    
    // TRANSPARENT INPUT FIELDS (instead of white)
    colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);         // TRANSPARENT input background
    colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.15f); // Slightly more visible on hover
    colors[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);   // More visible when typing
    
    colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.2f);
    
    // WHITE SCROLLBAR
    colors[ImGuiCol_ScrollbarBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);     // Light white background
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);   // Gray grab
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Darker hover
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);  // Darkest active
    
    colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    
    // WHITE BUTTONS
    colors[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.9f);          // WHITE button background
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);   // Light gray hover
    colors[ImGuiCol_ButtonActive] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);    // Gray active
    
    colors[ImGuiCol_Header] = ImVec4(0.4f, 0.4f, 0.4f, 0.3f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.5f, 0.5f, 0.5f, 0.8f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.6f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.8f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.6f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.9f);
    colors[ImGuiCol_Tab] = ImVec4(0.4f, 0.4f, 0.4f, 0.8f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.9f, 0.7f, 0.0f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.9f, 0.7f, 0.0f, 1.0f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.0f, 1.0f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.9f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.2f, 0.2f, 0.2f, 0.35f);
    
    // Style settings
    style.WindowPadding = ImVec2(15, 15);
    style.ItemSpacing = ImVec2(8, 8);
    style.FramePadding = ImVec2(8, 6);
    style.WindowRounding = 8.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.ScrollbarSize = 16.0f;
    style.ScrollbarRounding = 4.0f;
}

class CleanCCNAMenu {
private:
    std::vector<CCNAQuestion> questions;
    std::vector<int> searchResults;
    char searchBuffer[512] = {0};
    int selectedResult = 0;
    bool databaseLoaded = false;

public:
    bool LoadFromTextFile() {
        std::ifstream file("ccna_questions.txt");
        if (!file.is_open()) {
            DBGPRINT("[#] Could not open ccna_questions.txt\n");
            return false;
        }
        
        questions.clear();
        std::string line;
        CCNAQuestion currentQ;
        bool inQuestion = false;
        
        while (std::getline(file, line)) {
            if (line.find("Q#") == 0) {
                if (inQuestion && !currentQ.question.empty()) {
                    questions.push_back(currentQ);
                }
                currentQ = CCNAQuestion();
                currentQ.id = static_cast<int>(questions.size()) + 1;
                currentQ.question = line.substr(2);
                inQuestion = true;
            }
            else if (line.find("A:") == 0) {
                currentQ.correctAnswers.push_back(line.substr(2));
            }
            else if (line.find("URL:") == 0) {
                currentQ.sourceUrl = line.substr(4);
            }
        }
        
        if (inQuestion && !currentQ.question.empty()) {
            questions.push_back(currentQ);
        }
        
        file.close();
        databaseLoaded = true;
        DBGPRINT("[#] Loaded %zu CCNA questions\n", questions.size());
        return true;
    }

    void Search() {
        searchResults.clear();
        selectedResult = 0;
        
        if (strlen(searchBuffer) < 2) return;
        
        std::string searchLower = searchBuffer;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
        
        for (size_t i = 0; i < questions.size(); i++) {
            std::string questionLower = questions[i].question;
            std::transform(questionLower.begin(), questionLower.end(), questionLower.begin(), ::tolower);
            
            if (questionLower.find(searchLower) != std::string::npos) {
                searchResults.push_back(static_cast<int>(i));
            }
        }
        
        DBGPRINT("[#] Found %zu results for '%s'\n", searchResults.size(), searchBuffer);
    }

    void DrawMenu() {
        // Apply the global style every frame to ensure it sticks
        SetOverlayStyle();
        
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.1f, io.DisplaySize.y * 0.1f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
        
        ImGui::Begin("CCNA Search Tool - TarekBtw", nullptr, ImGuiWindowFlags_None);
        
        ImGui::Text("Questions Available: %zu", questions.size());
        ImGui::Separator();
        
        if (!databaseLoaded) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 0.8f), "Database not loaded!");
            ImGui::Text("Make sure 'ccna_questions.txt' is in the same folder.");
            
            // Override text color for button visibility on white background
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // BLACK text
            if (ImGui::Button("Reload Database")) {
                LoadFromTextFile();
            }
            ImGui::PopStyleColor();
            
        } else {
            // Search section
            ImGui::Text("Search:");
            ImGui::SetNextItemWidth(-1);
            
            // Transparent input field - text uses normal overlay text color (semi-transparent white)
            if (ImGui::InputText("##search", searchBuffer, sizeof(searchBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                Search();
            }
            
            ImGui::Text("Results: %zu matches", searchResults.size());
            
            if (!searchResults.empty()) {
                ImGui::Separator();
                
                // Scrollable content area - show first result only
                float contentHeight = ImGui::GetContentRegionAvail().y - 20;
                ImGui::BeginChild("QuestionContent", ImVec2(0, contentHeight), true, 
                                ImGuiWindowFlags_AlwaysVerticalScrollbar);
                
                // Always show the first search result (index 0)
                const CCNAQuestion& q = questions[searchResults[0]];
                
                // Colored text for sections
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 0.8f), "Question #%d:", q.id); // Blue
                
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextWrapped("%s", q.question.c_str());
                ImGui::PopTextWrapPos();
                
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 0.8f), "Correct Answer(s):"); // Green
                
                ImGui::PushTextWrapPos(0.0f);
                for (size_t i = 0; i < q.correctAnswers.size(); i++) {
                    ImGui::Text("   %zu.", i + 1);
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", q.correctAnswers[i].c_str());
                }
                ImGui::PopTextWrapPos();
                ImGui::EndChild();
                
            } else if (strlen(searchBuffer) > 0) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 0.8f), "No questions found matching '%s'", searchBuffer); // Orange
            }
        }
        
        ImGui::End();
    }
};

static CleanCCNAMenu g_menu;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    DBGPRINT("[#] Clean CCNA Menu: Starting (User: TarekBtw, Time: 2025-06-06 17:26:46 UTC)\n");

    // Load database
    g_menu.LoadFromTextFile();
    
    // Register hotkey for toggle
    RegisterHotKey(NULL, 1, 0, VK_INSERT);

    // Get full screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Create window class
    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
        GetModuleHandle(NULL), NULL, NULL, NULL, NULL,
        _T("CleanCCNAMenu"), NULL
    };
    RegisterClassEx(&wc);

    // Create layered window for transparency
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        wc.lpszClassName, _T(""),
        WS_POPUP,
        0, 0, screenWidth, screenHeight,
        NULL, NULL, wc.hInstance, NULL
    );
    
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    g_OverlayHwnd = hwnd;

    // D3D11 setup
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = screenWidth;
    sd.BufferDesc.Height = screenHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    D3D_FEATURE_LEVEL featureLevel;
    
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &swapChain, &device, &featureLevel, &context
    );
    
    if (FAILED(hr)) {
        DBGPRINT("[#] D3D11 creation failed: 0x%08X\n", hr);
        return 1;
    }

    ID3D11RenderTargetView* mainRenderTargetView = nullptr;
    {
        ID3D11Texture2D* pBackBuffer = nullptr;
        swapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        device->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
        pBackBuffer->Release();
    }

    // ImGui setup
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // SET THE OVERLAY STYLE IMMEDIATELY AFTER CONTEXT CREATION
    SetOverlayStyle();
    
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);

    ShowWindow(hwnd, SW_SHOW);

    bool menuVisible = true;
    MSG msg;
    bool running = true;

    DBGPRINT("[#] Clean CCNA Menu ready! Press INSERT to toggle\n");

    // Main loop
    while (running) {
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && msg.wParam == 1) {
                menuVisible = !menuVisible;
                DBGPRINT("[#] Menu visibility: %s\n", menuVisible ? "VISIBLE" : "HIDDEN");
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
            }
        }

        if (menuVisible) {
            ShowWindow(hwnd, SW_SHOW);
        } else {
            ShowWindow(hwnd, SW_HIDE);
        }

        // Render
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (menuVisible) {
            g_menu.DrawMenu();
        }

        ImGui::EndFrame();
        ImGui::Render();

        // Transparent clear
        const float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
        context->ClearRenderTargetView(mainRenderTargetView, clear_col);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain->Present(1, 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Cleanup
    UnregisterHotKey(NULL, 1);
    DBGPRINT("[#] Clean CCNA Menu shutting down\n");
    
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    if (mainRenderTargetView) mainRenderTargetView->Release();
    if (swapChain) swapChain->Release();
    if (context) context->Release();
    if (device) device->Release();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}