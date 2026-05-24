#include "gui.h"
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <vector>
#include <cmath>
#include <string>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"
#include "map.h"
#include <filesystem>

LocationData g_data;
std::mutex g_mutex;
std::map<int, ScrollingBuffer> g_rsrp_buffers, g_rsrq_buffers, g_rssnr_buffers;
std::mutex g_buffer_mutex;

static ImVec4 g_colors[] = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,0,1}, {1,0,1,1}, {0,1,1,1}};

void run_gui() {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* w = SDL_CreateWindow("Mobile Server", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl = SDL_GL_CreateContext(w);
    glewInit();
    
    IMGUI_CHECKVERSION(); 
    ImGui::CreateContext(); 
    ImPlot::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(w, gl); 
    ImGui_ImplOpenGL3_Init("#version 330");

    bool running = true;
    static float timer = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) { 
            ImGui_ImplSDL2_ProcessEvent(&e); 
            if (e.type == SDL_QUIT) running = false; 
        }

        tileUpdate();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize);

        if (ImGui::BeginTabBar("Tabs")) {
            if (ImGui::BeginTabItem("Signal Graphs")) {
                ImGui::Columns(2); ImGui::SetColumnWidth(0, 300);
                LocationData loc; { std::lock_guard<std::mutex> lk(g_mutex); loc = g_data; }
                ImGui::Text("Lat: %.6f\nLon: %.6f\nTime: %s", loc.lat, loc.lon, loc.time.c_str());
                ImGui::Separator();
                for (auto& tw : loc.towers) ImGui::Text("PCI: %d RSRP: %d", tw.pci, tw.rsrp);
                ImGui::NextColumn();
                
                timer += ImGui::GetIO().DeltaTime;
                auto plot = [&](const char* name, auto& buffers, float min, float max) {
                    if (ImPlot::BeginPlot(name, {-1, 200})) {
                        ImPlot::SetupAxes("Time", "Val");
                        ImPlot::SetupAxisLimits(ImAxis_X1, timer-60, timer, ImGuiCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, min, max);
                        std::lock_guard<std::mutex> lk(g_buffer_mutex);
                        int i = 0;
                        for (auto& [pci, buf] : buffers) {
                            std::vector<float> px, py;
                            for(auto& p : buf.Data) { px.push_back(p.x); py.push_back(p.y); }
                            ImPlot::SetNextLineStyle(g_colors[i++ % 6]);
                            ImPlot::PlotLine(std::to_string(pci).c_str(), px.data(), py.data(), (int)px.size());
                        }
                        ImPlot::EndPlot();
                    }
                };
                plot("RSRP", g_rsrp_buffers, -140, -60);
                plot("RSRQ", g_rsrq_buffers, -20, -3);
                ImGui::Columns(1); ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("OSM Map")) {

                ImGui::NextColumn();
                ImVec2 sz = ImGui::GetContentRegionAvail();
                if (ImPlot::BeginPlot("##Map", sz, ImPlotFlags_CanvasOnly)) {
                    ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
                    
                    double dx = 360.0 / pow(2, g_mapZoom);
                    double dy = 170.0 / pow(2, g_mapZoom);
                    
                    ImPlot::SetupAxisLimits(ImAxis_X1, g_mapCenterLon - dx, g_mapCenterLon + dx, ImGuiCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, g_mapCenterLat - dy, g_mapCenterLat + dy, ImGuiCond_Always);

                    handleMapInput(sz, dx, dy); 
                    renderTiles((int)sz.x, (int)sz.y);   

                    ImPlot::EndPlot();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
        ImGui::Render();
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(w);
    }

    g_tileWorkerRunning = false;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(w);
    SDL_Quit();
}