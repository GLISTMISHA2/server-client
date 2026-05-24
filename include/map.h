#ifndef MAP_H
#define MAP_H

#include <GL/glew.h>
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <mutex>
#include <imgui.h>

struct TextureData {
    GLuint id = 0;
    std::vector<unsigned char> rgbaBlob;
    int width = 0, height = 0;
};

struct TileJob {
    std::string id;
    int zoom, x, y;
};

extern bool g_tileWorkerRunning;
extern std::map<std::string, TextureData> g_tileCache;
extern std::queue<TileJob> g_tileQueue;
extern std::mutex g_tileCacheMutex, g_tileQueueMutex;

extern int g_mapZoom;
extern double g_mapCenterLat;
extern double g_mapCenterLon;
extern bool g_tileWorkerRunning;


double lon2x(double lon, int z);
double lat2y(double lat, int z);
double x2lon(double x, int z);
double y2lat(double y, int z);

std::string makeUrl(int z, int x, int y);
size_t onPullResponse(void *data, size_t size, size_t nmemb, void *userp);
bool receiveTile(int z, int x, int y, std::vector<unsigned char>& blob);
void glLoad(GLuint& id, unsigned char* data, int width, int height);

void tile_loader_thread();
void renderTiles(int winW, int winH);
void tileUpdate();
void handleMapInput(ImVec2 size, double dx, double dy);

#endif