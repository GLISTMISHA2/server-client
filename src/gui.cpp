#include "gui.h"
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <mutex>
#include <filesystem>


//НАдо бы переделать немного граыики. делать не через буфер а через массив 






#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"
#include "map.h"
#include "heatmap.h"

extern std::vector<LocationData> g_loadedLocations;
extern std::map<std::string, TextureData> g_tileCache;
extern std::mutex g_tileCacheMutex;

LocationData g_data;
std::mutex g_mutex;
std::map<int, ScrollingBuffer> g_rsrp_buffers, g_rsrq_buffers, g_rssnr_buffers;
std::mutex g_buffer_mutex;

static ImVec4 g_colors[] = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,0,1}, {1,0,1,1}, {0,1,1,1}};

static std::vector<int> getUniqueEarfcnList(const std::vector<LocationData>& points) {
    std::vector<int> list;
    for (const auto& pt : points) {
        for (const auto& tw : pt.towers) {
            if (std::find(list.begin(), list.end(), tw.earfcn) == list.end()) {
                list.push_back(tw.earfcn);
            }
        }
    }

    std::sort(list.begin(), list.end());
    return list;
}

static void resetHeatmapRenderCache() {
    clearHeatmapQueue();
    
    std::lock_guard<std::mutex> lock(g_tileCacheMutex);
    for (auto it = g_tileCache.begin(); it != g_tileCache.end();) {
        if (it->first.find("_heat") != std::string::npos) {
            if (it->second.id != 0) {
                glDeleteTextures(1, &it->second.id);
            }

            std::string filePath = "build/" + it->first + ".png";
            std::filesystem::remove(filePath);
            it = g_tileCache.erase(it);
        } else {
            ++it;
        }
    }
}

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

    std::vector<int> earfcnList = getUniqueEarfcnList(g_loadedLocations);
    static int selectedEarfcnIdx = 0;
    if (!earfcnList.empty()) {
        g_selectedEarfcn = earfcnList[selectedEarfcnIdx];
    }

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
                ImGui::Columns(1); 
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("OSM Map")) {

                ImGui::Columns(2); 
                ImGui::SetColumnWidth(0, 320);

                ImGui::Text("Heatmap Engine Settings");
                ImGui::Separator();
                ImGui::Text("Loaded points: %zu", g_loadedLocations.size());
                ImGui::Spacing();

                const char* criteriaNames[] = { "RSRP (дБм)", "RSRQ (дБ)", "RSSI (дБм)", "Altitude (м)" };
                int currentItem = static_cast<int>(g_currentCriterion);
                if (ImGui::Combo("Criterion", &currentItem, criteriaNames, IM_ARRAYSIZE(criteriaNames))) {
                    g_currentCriterion = currentItem;
                    resetHeatmapRenderCache();
                }
                ImGui::Spacing();

                if (!earfcnList.empty()) {
                    if (ImGui::BeginCombo("Select EARFCN", std::to_string(earfcnList[selectedEarfcnIdx]).c_str())) {
                        for (int n = 0; n < (int)earfcnList.size(); n++) {
                            bool is_selected = (selectedEarfcnIdx == n);
                            if (ImGui::Selectable(std::to_string(earfcnList[n]).c_str(), is_selected)) {
                                selectedEarfcnIdx = n;
                                g_selectedEarfcn = earfcnList[n];
                                resetHeatmapRenderCache();
                            }
                            if (is_selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: No EARFCN data inside JSON!");
                }
                ImGui::Spacing();

                if (ImGui::SliderFloat("Radius (m)", &g_interpolationRadius, 10.0f, 40.0f, "%.1f meters")) {
                    resetHeatmapRenderCache();
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Thread pool: %zu jobs pending", g_heatQueue.size());

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
                
                ImGui::Columns(1);
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