#include "heatmap.h"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iostream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

//EARFCN - номер частотного канала
//RSSI - полная мощность которая принимает антенна 
//RSRP - мощность полезного сигнала
//RSRQ - качество сигнала


std::queue<HeatmapJob> g_heatQueue;
std::mutex g_heatQueueMutex;
bool g_heatWorkerRunning = true;  

int g_currentCriterion = HeatmapCriterion::RSRP;
int g_selectedEarfcn = 0;
float g_interpolationRadius = 30.0f; 

double x2lon(double x, int z) { 
    return x / (double)pow(2, z) * 360.0 - 180.0; 
}

double y2lat(double y, int z) {
    double n = M_PI - 2.0 * M_PI * y / (double)pow(2, z);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

double getDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
    const double METERS_PER_LAT = 111132.92; 
    const double METERS_PER_LON = 111319.49 * std::cos(lat1 * M_PI / 180.0);
    
    double dy = (lat1 - lat2) * METERS_PER_LAT;
    double dx = (lon1 - lon2) * METERS_PER_LON;
    return std::sqrt(dx * dx + dy * dy);
}

double dbmToMilliwatts(double dbm) {
    return std::pow(10.0, dbm / 10.0);
}
double milliwattsToDbm(double mw) {
    if (mw <= 0.0) return -140.0;
    return 10.0 * std::log10(mw);
}
double dbToLinear(double db) {
    return std::pow(10.0, db / 10.0);
}
double linearToDb(double linear) {
    if (linear <= 0.0) return -30.0;
    return 10.0 * std::log10(linear);
}

RGBA getHeatmapColor(double value, double minVal, double maxVal) {
    if (g_currentCriterion == HeatmapCriterion::RSRP) {
        if (value < minVal) return {0, 0, 0, 0}; 
    }

    double ratio = (value - minVal) / (maxVal - minVal);
    ratio = std::clamp(ratio, 0.0, 1.0);
    
    RGBA c = {0, 0, 0, 180};
    
    if (ratio < 0.25) {
        c.r = 0; c.g = static_cast<unsigned char>(ratio * 4 * 255); c.b = 255;
    } else if (ratio < 0.5) {
        c.r = 0; c.g = 255; c.b = static_cast<unsigned char>((1.0 - (ratio - 0.25) * 4) * 255);
    } else if (ratio < 0.75) {
        c.r = static_cast<unsigned char>((ratio - 0.5) * 4 * 255); c.g = 255; c.b = 0;
    } else {
        c.r = 255; c.g = static_cast<unsigned char>((1.0 - (ratio - 0.75) * 4) * 255); c.b = 0;
    }
    return c;
}

bool getPointValue(const LocationData& pt, double& outVal) {//вытаскиеваем значение сигнала
    if (g_currentCriterion == HeatmapCriterion::ALTITUDE) {
        outVal = pt.alt;
        return true;
    }

    for (const auto& tw : pt.towers) {
        if (tw.earfcn == g_selectedEarfcn) {
            if (g_currentCriterion == HeatmapCriterion::RSRP) outVal = tw.rsrp;
            else if (g_currentCriterion == HeatmapCriterion::RSRQ) outVal = tw.rsrq;
            else if (g_currentCriterion == HeatmapCriterion::RSSI) outVal = tw.dbm;
            return true;
        }
    }
    return false;
}

bool calculatePixelIDW(double pixelLat, double pixelLon, const std::vector<const LocationData*>& points, RGBA& outColor) {
    double sumWeights = 0.0;
    double sumWeightedLinear = 0.0;

    double minVal = -110.0, maxVal = -80.0;
    if (g_currentCriterion == HeatmapCriterion::RSRQ) { minVal = -20.0; maxVal = -3.0; }
    else if (g_currentCriterion == HeatmapCriterion::RSSI) { minVal = -120.0; maxVal = -50.0; }
    else if (g_currentCriterion == HeatmapCriterion::ALTITUDE) { minVal = 100.0; maxVal = 300.0; }

    bool isLogarithmic = (g_currentCriterion == HeatmapCriterion::RSRP ||
                          g_currentCriterion == HeatmapCriterion::RSRQ ||
                          g_currentCriterion == HeatmapCriterion::RSSI);

    for (const LocationData* pt : points) {
        double val = 0.0;
        if (!getPointValue(*pt, val)) continue;

        double dist = getDistanceMeters(pixelLat, pixelLon, pt->lat, pt->lon);
        if (dist > g_interpolationRadius) continue;
        
        if (dist < 0.5) {
            outColor = getHeatmapColor(val, minVal, maxVal);
            return true;
        }
        
        double weight = 1.0 / (dist * dist);
        
        
        sumWeights += weight;

        if (isLogarithmic) {
            if (g_currentCriterion == HeatmapCriterion::RSRQ) {//взвешанное суммирование
                sumWeightedLinear += weight * dbToLinear(val);
            } else {
                sumWeightedLinear += weight * dbmToMilliwatts(val);
            }
        } else {
            sumWeightedLinear += weight * val;
        }
    }
    if (sumWeights == 0.0) return false;

    double avgLinear = sumWeightedLinear / sumWeights; //расчет средневзвешенного

    //накопленое значение делим на сумму всех весов 
    double finalVal;
    if (isLogarithmic) {
        if (g_currentCriterion == HeatmapCriterion::RSRQ) {
            finalVal = linearToDb(avgLinear);
        } else {
            finalVal = milliwattsToDbm(avgLinear);
        }
    } else {
        finalVal = avgLinear;
    }

    outColor = getHeatmapColor(finalVal, minVal, maxVal);
    return true;
}

std::vector<const LocationData*> filterPointsForTile(int z, int tx, int ty, const std::vector<LocationData>& allPoints) {
    double lonLeft = x2lon(tx, z);
    double lonRight = x2lon(tx + 1, z); // почему мы тут прибавляем единицу
    double latTop = y2lat(ty, z);
    double latBottom = y2lat(ty + 1, z);

    double marginDeg = g_interpolationRadius / 111000.0;
    lonLeft -= marginDeg;
    lonRight += marginDeg;
    latTop += marginDeg;
    latBottom -= marginDeg;

    std::vector<const LocationData*> filtered;
    filtered.reserve(allPoints.size() / 100);
    for (const auto& pt : allPoints) {
        if (pt.lat >= latBottom && pt.lat <= latTop &&
            pt.lon >= lonLeft && pt.lon <= lonRight) {
            filtered.push_back(&pt);
        }
    }
    return filtered;
}

void generateHeatmapTile(int z, int tx, int ty, const std::vector<LocationData>& allPoints) {
    std::string dirPath = "build/" + std::to_string(z) + "/" + std::to_string(tx);
    std::filesystem::create_directories(dirPath);
    std::string filePath = dirPath + "/" + std::to_string(ty) + "_heat.png";

    std::vector<const LocationData*> tilePoints = filterPointsForTile(z, tx, ty, allPoints);
    if (tilePoints.empty()) {
        std::filesystem::remove(filePath);
        return;
    }

    int w = 256, h = 256, channels = 4;
    std::vector<unsigned char> image(w * h * channels, 0);//черное прозрачное изображение

    double lonLeft = x2lon(tx, z);
    double lonRight = x2lon(tx + 1, z);
    double latTop = y2lat(ty, z);
    double latBottom = y2lat(ty + 1, z);

    bool hasData = false;

    for (int y = 0; y < h; ++y) {
        double currentLat = latTop + (double)y / (h - 1) * (latBottom - latTop);
        for (int x = 0; x < w; ++x) {
            double currentLon = lonLeft + (double)x / (w - 1) * (lonRight - lonLeft);
            
            RGBA pixelColor;
            if (calculatePixelIDW(currentLat, currentLon, tilePoints, pixelColor)) {// расчитыввется индекс пикселя по позиции x y
                int index = (y * w + x) * channels;
                image[index + 0] = pixelColor.r;
                image[index + 1] = pixelColor.g;
                image[index + 2] = pixelColor.b;
                image[index + 3] = pixelColor.a;
                if (pixelColor.a > 0) hasData = true;
            }
        }
    }

    if (hasData) {
        stbi_write_png(filePath.c_str(), w, h, channels, image.data(), w * channels);
    } else {
        std::filesystem::remove(filePath);
    }
}

void heatmapWorkerLoop(const std::vector<LocationData>& points) {
    while (g_heatWorkerRunning) {
        if (g_heatQueue.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        g_heatQueueMutex.lock();
        if (g_heatQueue.empty()) { g_heatQueueMutex.unlock(); continue; }
        HeatmapJob job = g_heatQueue.front();
        g_heatQueue.pop();
        g_heatQueueMutex.unlock();

        generateHeatmapTile(job.z, job.tx, job.ty, points);
    }
}

std::thread workerThread;

void startHeatmapWorker(const std::vector<LocationData>& points) {
    g_heatWorkerRunning = true;
    workerThread = std::thread(heatmapWorkerLoop, std::ref(points));
    workerThread.detach();
}

void stopHeatmapWorker() {
    g_heatWorkerRunning = false;
}

void clearHeatmapQueue() {
    std::lock_guard<std::mutex> lock(g_heatQueueMutex);
    std::queue<HeatmapJob> empty;
    std::swap(g_heatQueue, empty);
}