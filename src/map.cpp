#include "map.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <curl/curl.h>
#include <stb_image.h>
#include <implot.h>
#include "types.h"

int g_mapZoom = 16;
double g_mapCenterLat = 55.013; 
double g_mapCenterLon = 82.950;
bool g_mapDragging = false; 
bool g_tileWorkerRunning = true;

std::map<std::string, TextureData> g_tileCache;
std::queue<TileJob> g_tileQueue;
std::mutex g_tileCacheMutex, g_tileQueueMutex;

double lon2x(double lon, int z) { 
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
                g_tileCache[job.id].rgbaBlob.assign(data, data + w * h * 4);
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
    int startX = floor(cx) - tilesX / 2;
    int startY = floor(cy) - tilesY / 2;
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
if (!g_loadedLocations.empty()) {
        for (const auto& loc : g_loadedLocations) {

            int rsrp = -140; 
            if (!loc.towers.empty()) {
                rsrp = loc.towers[0].rsrp;
            }

            ImVec4 color;
            if (rsrp >= -85) {
                color = ImVec4(0.0f, 1.0f, 0.0f, 0.8f);
            } else if (rsrp <= -115) {
                color = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
            } else {
                float factor = (rsrp - (-115)) / 30.0f;
                color = ImVec4(1.0f - factor, factor, 0.0f, 0.8f);
            }

            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, color, 1.0f, color);
            
            double plotX = loc.lon;
            double plotY = loc.lat;

            ImPlot::PlotScatter("##json_nodes", &plotX, &plotY, 1);
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