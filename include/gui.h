#ifndef GUI_H
#define GUI_H

#include <mutex>
#include <map>
#include <deque>
#include "types.h"

struct ScrollingBuffer {
    struct Point { float x, y; };
    std::deque<Point> Data;
    void AddPoint(float x, float y) {
        Data.push_back({x, y});
        if (Data.size() > 1000) Data.pop_front();
    }
};

extern LocationData g_data;
extern std::mutex g_mutex;
extern std::map<int, ScrollingBuffer> g_rsrp_buffers, g_rsrq_buffers, g_rssnr_buffers;
extern std::mutex g_buffer_mutex;

void run_gui();

#endif