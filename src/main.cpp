#include <SDL2/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <SDL2/SDL_opengl.h>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

#include "network_mgr.h"
#include "gui_layer.h"

#ifdef _WIN32
/* 从 Win32 RC 资源加载图标并转换为 SDL_Surface，供 SDL_SetWindowIcon 使用 */
static SDL_Surface* LoadIconFromResource(int resource_id) {
    HICON hIcon = (HICON)LoadImageA(
        GetModuleHandleA(NULL),
        MAKEINTRESOURCEA(resource_id),
        IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    if (!hIcon) return nullptr;

    ICONINFO ii = {};
    if (!GetIconInfo(hIcon, &ii)) {
        DestroyIcon(hIcon);
        return nullptr;
    }

    BITMAP bm = {};
    GetObject(ii.hbmColor, sizeof(bm), &bm);
    int w = bm.bmWidth, h = bm.bmHeight;

    SDL_Surface* surf = SDL_CreateRGBSurface(
        0, w, h, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (surf) {
        HDC hdc = CreateCompatibleDC(NULL);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth       =  w;
        bi.bmiHeader.biHeight      = -h;  /* top-down */
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        GetDIBits(hdc, ii.hbmColor, 0, h, surf->pixels, &bi, DIB_RGB_COLORS);
        DeleteDC(hdc);
    }

    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    DestroyIcon(hIcon);
    return surf;
}
#endif

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("MStudio - MODUS Debug Workbench", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);

#ifdef _WIN32
    /* 加载 RC 资源中的图标 (IDI_ICON1 = 1) 并绑定到 SDL2 窗口 */
    SDL_Surface* icon_surf = LoadIconFromResource(1);
    if (icon_surf) {
        SDL_SetWindowIcon(window, icon_surf);
        SDL_FreeSurface(icon_surf);
    }
#endif
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; 

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Initialize networking
    NetworkMgr::GetInstance().Init();

    GuiLayer* gui_layer = new GuiLayer();

    // Main loop
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Render UI
        gui_layer->Render();

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup — destroy panels BEFORE shutting down network/Winsock
    delete gui_layer;

    NetworkMgr::GetInstance().Shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
