#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <string>
#include "types.h" // Твоя структура LocationData и CellTowerData

struct HeatmapCriterion {
    static const int RSRP = 0;
    static const int RSRQ = 1;
    static const int RSSI = 2;
    static const int ALTITUDE = 3;
};

struct RGBA {
    unsigned char r, g, b, a;
};

struct HeatmapJob {
    int z, tx, ty;
};

// Глобальные переменные управления (будут видны в GUI и map.cpp)
extern std::queue<HeatmapJob> g_heatQueue;
extern std::mutex g_heatQueueMutex;
extern bool g_heatWorkerRunning;

// Настройки хитмапа (меняются из ImGui)
extern int g_currentCriterion;
extern int g_selectedEarfcn;
extern float g_interpolationRadius; // 10.0f - 40.0f метров

// Запуск фонового потока вычислений
void startHeatmapWorker(const std::vector<LocationData>& points);
void stopHeatmapWorker();

// Сброс текущей очереди задач (вызывается при смене EARFCN или критерия)
void clearHeatmapQueue();