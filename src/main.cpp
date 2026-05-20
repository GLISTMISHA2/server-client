#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <ctime>
#include <vector>
#include <deque>
#include <mutex>
#include <map>
#include <queue>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <curl/curl.h>


#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include <stb_image.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <libpq-fe.h>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

#define HOST "localhost"
#define PORT "5432"
#define DB_NAME "test_db_from_psql"
#define DB_USER "postgres"
#define DB_USER_PASSWORD "misha666777"

using json = nlohmann::json;

struct CellTowerData {
    std::string type;
    int mcc = 0, mnc = 0, pci = 0, tac = 0;
    int band = 0, ci = 0, earfcn = 0, asu = 0, rsrp = 0, rsrq = 0, rssnr = 0;
    int dbm = 0;
};

struct LocationData {
    float lat = 0, lon = 0, alt = 0, accuracy = 0;
    std::string time;
    long long timestamp = 0;
    std::vector<CellTowerData> towers;
};

struct ScrollingBuffer {
    struct Point { float x, y; };
    std::deque<Point> Data;
    void AddPoint(float x, float y) {
        Data.push_back({x, y});
        if (Data.size() > 1000) Data.pop_front();
    }
};

struct TextureData {
    GLuint id = 0;
    std::vector<unsigned char> rgbaBlob;
    int width = 0, height = 0;

};

struct TileJob {
    std::string id;
    int zoom, x, y;
};

LocationData g_data;
std::mutex g_mutex;
std::map<int, ScrollingBuffer> g_rsrp_buffers, g_rsrq_buffers, g_rssnr_buffers;
std::mutex g_buffer_mutex;
ImVec4 g_colors[] = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,0,1}, {1,0,1,1}, {0,1,1,1}};

static int g_mapZoom = 16;
static double g_mapCenterLat = 55.013; 
static double g_mapCenterLon = 82.950;
static bool g_mapDragging = false;
static bool g_tileWorkerRunning = true;


std::map<std::string, TextureData> g_tileCache;
std::queue<TileJob> g_tileQueue;
std::mutex g_tileCacheMutex, g_tileQueueMutex;


double lon2x(double lon, int z) { //долгота - номер тайла
    return (lon + 180.0) / 360.0 * pow(2, z); 
}

double lat2y(double lat, int z) {
    double lat_rad = lat * M_PI / 180.0;
    return (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * pow(2, z);
}

double x2lon(double x, int z) { 
    return x / (double)pow(2, z) * 360.0 - 180.0; 
}

double y2lat(double y, int z) {
    double n = M_PI - 2.0 * M_PI * y / (double)pow(2, z);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

std::string makeUrl(int z, int x, int y) {
    std::ostringstream urlmaker;
    urlmaker << "https://tile.openstreetmap.org/" << z << '/' << x << '/' << y << ".png";
    return urlmaker.str();
}

size_t onPullResponse(void *data, size_t size, size_t nmemb, void *userp) {
    size_t realsize{size * nmemb};
    auto &blob{*static_cast<std::vector<unsigned char> *>(userp)};
    auto const *const dataptr{static_cast<unsigned char *>(data)};
    blob.insert(blob.cend(), dataptr, dataptr + realsize);
    return realsize;
}

bool receiveTile(int z, int x, int y, std::vector<unsigned char>& blob) {
    std::string tilePath = "build/" + std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png";
    if (std::filesystem::exists(tilePath)) {
        std::ifstream file(tilePath, std::ios::binary);
        blob.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return true;
    }
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, makeUrl(z, x, y).c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MobileServer/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&blob);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onPullResponse);
    bool ok = curl_easy_perform(curl) == CURLE_OK;
    curl_easy_cleanup(curl);
    if (ok) {
        std::filesystem::create_directories("build/" + std::to_string(z) + "/" + std::to_string(x));
        std::ofstream file(tilePath, std::ios::binary);
        file.write((char*)blob.data(), blob.size());
    }
    return ok;
}

void glLoad(GLuint& id, unsigned char* data, int width, int height) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

void tile_loader_thread() {
    while (g_tileWorkerRunning) {
        if (g_tileQueue.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        g_tileQueueMutex.lock();
        TileJob job = g_tileQueue.front();
        g_tileQueue.pop();
        g_tileQueueMutex.unlock();

        std::vector<unsigned char> blob;
        if (receiveTile(job.zoom, job.x, job.y, blob)) {
            int w, h, ch;
            unsigned char* data = stbi_load_from_memory(blob.data(), blob.size(), &w, &h, &ch, 4);
            if (data) {
                g_tileCacheMutex.lock();
                g_tileCache[job.id].rgbaBlob.assign(data,data+w*h*4);
                g_tileCache[job.id].width = w;
                g_tileCache[job.id].height = h;
                stbi_image_free(data);
                g_tileCacheMutex.unlock();
            }
        }
    }
}

void renderTiles(int winW, int winH) {
    double cx = lon2x(g_mapCenterLon, g_mapZoom);
    double cy = lat2y(g_mapCenterLat, g_mapZoom);
    int tilesX = (winW / 256); 
    int tilesY = (winH / 256); 
    int startX = floor(cx)- tilesX / 2;
    int startY = floor(cy)- tilesY / 2;
    for (int tx = startX; tx < startX + tilesX; tx++) {
        for (int ty = startY; ty < startY + tilesY; ty++) {
            std::string tileId = std::to_string(g_mapZoom) + "/" + std::to_string(tx) + "/" + std::to_string(ty);
            
            g_tileCacheMutex.lock();
            if (g_tileCache.find(tileId) == g_tileCache.end()) {
                g_tileQueueMutex.lock();
                g_tileQueue.push({tileId, g_mapZoom, tx, ty});
                g_tileQueueMutex.unlock();
            }
            
            GLuint id = g_tileCache[tileId].id;
            g_tileCacheMutex.unlock();

            if (id != 0) {
                ImPlot::PlotImage(tileId.c_str(), (ImTextureID)(intptr_t)id,
                    ImPlotPoint{x2lon(tx, g_mapZoom), y2lat(ty + 1, g_mapZoom)},
                    ImPlotPoint{x2lon(tx + 1, g_mapZoom), y2lat(ty, g_mapZoom)});
            }
        }
    }
}

void tileUpdate() {
    g_tileCacheMutex.lock();
    for (auto& [key, tex] : g_tileCache) {
        if (!tex.rgbaBlob.empty()) {
            glLoad(tex.id, tex.rgbaBlob.data(), tex.width, tex.height);
            tex.rgbaBlob.clear();
        }
    }
    g_tileCacheMutex.unlock();
}
void handleMapInput(ImVec2 size, double dx, double dy) {
    if (!ImPlot::IsPlotHovered()) return;

    g_mapZoom = std::clamp(g_mapZoom + (int)ImGui::GetIO().MouseWheel, 0, 19);

    if (ImGui::IsMouseDragging(0)) {
        g_mapDragging = true;
        ImVec2 delta = ImGui::GetMouseDragDelta(0);
        g_mapCenterLon -= delta.x * dx / size.x;
        g_mapCenterLat += delta.y * dy / size.y;
        ImGui::ResetMouseDragDelta(0);
    } else {
        g_mapDragging = false;
    }
}


int getIntSafely(const json& j, const std::string& key, int d = 0) {
    if (!j.contains(key)) return d;
    if (j[key].is_number()) return j[key];
    if (j[key].is_string()) { try { return std::stoi(j[key].get<std::string>()); } catch (...) {} }
    return d;
}

CellTowerData parseTower(const json& j, const std::string& type) {
    CellTowerData t; t.type = type;
    t.mcc = getIntSafely(j, "mcc");
    t.mnc = getIntSafely(j, "mnc");
    t.pci = getIntSafely(j, "pci");
    t.tac = getIntSafely(j, "tac");
    if (type == "LTE") {
        t.ci = getIntSafely(j, "ci");
        t.earfcn = getIntSafely(j, "earfcn");
        t.asu = getIntSafely(j, "asu");
        t.rsrp = getIntSafely(j, "rsrp");
        t.rsrq = getIntSafely(j, "rsrq");
        t.rssnr = getIntSafely(j, "rssnr");
        t.dbm = getIntSafely(j, "dbm");
        t.band = getIntSafely(j, "band");
    }
    return t;
}

void saveToJsonFile(const LocationData& data) {
    try {
        json j;
        j["lat"] = data.lat; j["lon"] = data.lon; j["time"] = data.time;
        json cells = json::array();
        for (auto& t : data.towers) {
            json c = {{"type", t.type}, {"pci", t.pci}, {"rsrp", t.rsrp}};
            cells.push_back(c);
        }
        j["cells"] = cells;
        std::ofstream out("mobile_data.json", std::ios::app);
        out << j.dump() << std::endl;
    } catch (...) {}
}

void insertToDatabase(PGconn* db_con, const LocationData& data) {
    if (!db_con || PQstatus(db_con) != CONNECTION_OK) return;
    for (const auto& t : data.towers) {
        if (t.type != "LTE") continue;
        const char* p[17];
        char b[17][64];
        snprintf(b[0],64,"%f",data.lat); 
        snprintf(b[1],64,"%f",data.lon);
        snprintf(b[2],64,"%f",data.alt); 
        snprintf(b[3],64,"%f",data.accuracy);
        snprintf(b[4],64,"%lld",data.timestamp); 
        snprintf(b[5],64,"%d",t.pci);
        snprintf(b[6],64,"%d",t.rsrp); 
        snprintf(b[7],64,"%d",t.rsrq);
        snprintf(b[8],64,"%d",t.rssnr); 
        snprintf(b[9],64,"%d",t.mcc);
        snprintf(b[10],64,"%d",t.mnc); 
        snprintf(b[11],64,"%d",t.tac);
        snprintf(b[12],64,"%s",t.type.c_str()); 
        snprintf(b[13],64,"%d",t.earfcn);
        snprintf(b[14],64,"%d",t.asu); 
        snprintf(b[15],64,"%d",t.ci);
        snprintf(b[16],64,"%d",t.dbm);
        for(int i=0; i<17; i++) p[i] = b[i];
        
        const char* q = "INSERT INTO mobile_data (lat,lon,alt,accuracy,timestamp,pci,rsrp,rsrq,rssnr,mcc,mnc,tac,type,earfcn,asu,ci,dbm) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17)";
        PQclear(PQexecParams(db_con, q, 17, NULL, p, NULL, NULL, 0));
    }
}


void run_gui() {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* w = SDL_CreateWindow("Mobile Server", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl = SDL_GL_CreateContext(w);
    glewInit();
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImPlot::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(w, gl); ImGui_ImplOpenGL3_Init("#version 330");

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
                        int i=0;
                        for (auto& [pci, buf] : buffers) {
                            std::vector<float> px, py;
                            for(auto& p : buf.Data) { px.push_back(p.x); py.push_back(p.y); }
                            ImPlot::SetNextLineStyle(g_colors[i++%6]);
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

void run_server(PGconn* db) {
    zmq::context_t ctx(1); zmq::socket_t sock(ctx, zmq::socket_type::rep);
    sock.bind("tcp://*:5555"); sock.set(zmq::sockopt::rcvtimeo, 1000);
    float tc = 0;
    while (g_tileWorkerRunning) {
        zmq::message_t req;
        if (!sock.recv(req, zmq::recv_flags::none)) continue;
        try {
            auto j = json::parse(std::string((char*)req.data(), req.size()));
            LocationData nd;
            auto& it = j["data"][0]; auto& loc = it["locations"][0];
            nd.lat=loc["lat"]; nd.lon=loc["lon"]; nd.timestamp=loc["time"];
            time_t s = nd.timestamp/1000; char b[64]; strftime(b,64,"%H:%M:%S",localtime(&s)); nd.time=b;
            for(auto& x : it["cell_info"]["lte"]) nd.towers.push_back(parseTower(x,"LTE"));
            
            { std::lock_guard<std::mutex> lk(g_mutex); g_data=nd; }
            { std::lock_guard<std::mutex> lk(g_buffer_mutex);
                for(auto& t : nd.towers) {
                    g_rsrp_buffers[t.pci].AddPoint(tc, t.rsrp);
                    g_rsrq_buffers[t.pci].AddPoint(tc, t.rsrq);
                } tc += 1.0f;
            }
            insertToDatabase(db, nd); saveToJsonFile(nd);
            sock.send(zmq::buffer("OK"), zmq::send_flags::none);
        } catch(...) { sock.send(zmq::buffer("ERR"), zmq::send_flags::none); }
    }
}

int main() {

    curl_global_init(CURL_GLOBAL_ALL);
    std::filesystem::create_directories("build");

    PGconn* con = PQconnectdb("host=" HOST " port=" PORT " dbname=" DB_NAME " user=" DB_USER " password=" DB_USER_PASSWORD);
    
    std::thread(tile_loader_thread).detach();
    std::thread(run_server, con).detach();
    
    run_gui();
    PQfinish(con); curl_global_cleanup();
    return 0;
}