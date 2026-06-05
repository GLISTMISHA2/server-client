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
#include "types.h"
#include "gui.h"
//#include <stb_image.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <libpq-fe.h>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"
#include "map.h"
#include "heatmap.h"

#define HOST "localhost"
#define PORT "5432"
#define DB_NAME "test_db_from_psql"
#define DB_USER "postgres"
#define DB_USER_PASSWORD "misha666777"

void insertToDatabase(PGconn* db_con, const LocationData& data); 
CellTowerData parseTower(const nlohmann::json& j, const std::string& type);

using json = nlohmann::json;


void run_server(PGconn* db_con);


int main() {
    loadAllSamplesFromJson("build/mobile_data.json");
    curl_global_init(CURL_GLOBAL_ALL);
    std::filesystem::create_directories("build");
    startHeatmapWorker(g_loadedLocations);

    PGconn* con = PQconnectdb("host=" HOST " port=" PORT " dbname=" DB_NAME " user=" DB_USER " password=" DB_USER_PASSWORD);
    
    std::thread(tile_loader_thread).detach();
    std::thread(run_server, con).detach();
    
    run_gui();
    PQfinish(con); curl_global_cleanup();
    stopHeatmapWorker();
    
    return 0;
}