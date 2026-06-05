#include "types.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>
#include <filesystem>

std::vector<LocationData> g_loadedLocations;
using json = nlohmann::json;

int getIntSafely(const json& j, const std::string& key, int d) {
    if (!j.contains(key)) return d;
    if (j[key].is_number()) return j[key];
    if (j[key].is_string()) { 
        try { return std::stoi(j[key].get<std::string>()); } catch (...) {} 
    }
    return d;
}

CellTowerData parseTower(const json& j, const std::string& type) {
    CellTowerData t; 
    t.type = type;
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
        j["lat"] = data.lat; 
        j["lon"] = data.lon; 
        j["time"] = data.time;
        
        json cells = json::array();
        for (auto& t : data.towers) {
            json c = {{"type", t.type}, {"pci", t.pci}, {"rsrp", t.rsrp}};
            cells.push_back(c);
        }
        j["cells"] = cells;
        
        std::ofstream out("mobile_data.json", std::ios::app);
        out << j.dump() << std::endl;
    } catch (...) {
    }
}
void loadAllSamplesFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cout << "Не удалось открыть файл: " << filepath << std::endl;
        return;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cout << "Ошибка синтаксиса JSON: " << e.what() << std::endl;
        return;
    }

    int loadedCount = 0;

    for (const auto& item : root["data"]) {
        LocationData loc;

        if (item.contains("locations") && item["locations"].is_array() && !item["locations"].empty()) {
            const auto& firstLoc = item["locations"][0];
            loc.lat = firstLoc.value("lat", 0.0);
            loc.lon = firstLoc.value("lon", 0.0);
            loc.alt = firstLoc.value("alt", 0.0);
            loc.accuracy = firstLoc.value("accuracy", 0.0);

            long long ts = firstLoc.value("time", 0LL);
            loc.timestamp = ts;
            loc.time = std::to_string(ts);
        }

        if (loc.lat == 0.0 || loc.lon == 0.0) {
            continue;
        }

        if (item.contains("cell_info") && item["cell_info"].contains("lte") && item["cell_info"]["lte"].is_array()) {
            for (const auto& cellJson : item["cell_info"]["lte"]) {
                CellTowerData tower;
                tower.type = cellJson.value("type", "LTE");
                tower.pci  = cellJson.value("pci", 0);
                tower.ci   = cellJson.value("ci", 0);
                tower.tac  = cellJson.value("tac", 0);
                tower.earfcn = cellJson.value("earfcn", 0);
                tower.rsrp = cellJson.value("rsrp", 0);
                tower.rsrq = cellJson.value("rsrq", 0);
                tower.rssnr = cellJson.value("rssnr", 0);
                tower.dbm  = cellJson.value("dbm", 0);
                tower.asu  = cellJson.value("asu", 0);

                std::string mccStr = cellJson.value("mcc", "");
                std::string mncStr = cellJson.value("mnc", "");
                try {
                    tower.mcc = mccStr.empty() ? 0 : std::stoi(mccStr);
                    tower.mnc = mncStr.empty() ? 0 : std::stoi(mncStr);
                } catch (...) {
                    tower.mcc = 0;
                    tower.mnc = 0;
                }

                loc.towers.push_back(tower);
            }
        }

        g_loadedLocations.push_back(loc);
        loadedCount++;
    }

    std::cout << "Успешно загружено точек из JSON: " << loadedCount << std::endl;
}