#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct CellTowerData {
    std::string type;
    int mcc = 0; int mnc = 0; int pci = 0; int tac = 0;
    int ci = 0; int earfcn = 0; int asu = 0; int rsrp = 0;
    int rsrq = 0; int rssnr = 0; int dbm = 0; int band = 0;
};

struct LocationData {
    double lat = 0.0;
    double lon = 0.0;
    double alt = 0.0;
    double accuracy = 0.0;
    long long timestamp = 0;
    std::string time;
    std::vector<CellTowerData> towers;
};
extern std::vector<LocationData> g_loadedLocations;

int getIntSafely(const nlohmann::json& j, const std::string& key, int d = 0);
CellTowerData parseTower(const nlohmann::json& j, const std::string& type);
void saveToJsonFile(const LocationData& data);
void loadAllSamplesFromJson(const std::string& filepath);
#endif