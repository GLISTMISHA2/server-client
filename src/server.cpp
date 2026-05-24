#include "types.h"
#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <ctime>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <libpq-fe.h>
#include "gui.h"

using json = nlohmann::json;

extern LocationData g_data;
extern std::mutex g_mutex;


extern std::map<int, ScrollingBuffer> g_rsrp_buffers, g_rsrq_buffers, g_rssnr_buffers;
extern std::mutex g_buffer_mutex;
extern bool g_tileWorkerRunning; 

void insertToDatabase(PGconn* db_con, const LocationData& data);

void run_server(PGconn* db_con) {
    zmq::context_t ctx(1); 
    zmq::socket_t sock(ctx, zmq::socket_type::rep);
    sock.bind("tcp://*:5555"); 
    sock.set(zmq::sockopt::rcvtimeo, 1000); 
    
    float tc = 0.0f; 

    std::cout << "started on port 5555." << std::endl;

    while (g_tileWorkerRunning) {
        zmq::message_t req;
        if (!sock.recv(req, zmq::recv_flags::none)) continue;
        
        try {
            auto j = json::parse(std::string((char*)req.data(), req.size()));
            LocationData nd;
            
            auto& it = j["data"][0]; 
            auto& loc = it["locations"][0];
            
            nd.lat = loc["lat"]; 
            nd.lon = loc["lon"]; 
            nd.timestamp = loc["time"];
            
            time_t s = nd.timestamp / 1000; 
            char b[64]; 
            struct tm* timeinfo = localtime(&s);
            if (timeinfo) {
                strftime(b, 64, "%H:%M:%S", timeinfo); 
                nd.time = b;
            } else {
                nd.time = "00:00:00";
            }
            
            for (auto& x : it["cell_info"]["lte"]) {
                nd.towers.push_back(parseTower(x, "LTE"));
            }

            { 
                std::lock_guard<std::mutex> lk(g_mutex); 
                g_data = nd; 
            }

            { 
                std::lock_guard<std::mutex> lk(g_buffer_mutex);
                for (auto& t : nd.towers) {
                    if (t.pci != 0) {
                        g_rsrp_buffers[t.pci].AddPoint(tc, t.rsrp);
                        g_rsrq_buffers[t.pci].AddPoint(tc, t.rsrq);
                        g_rssnr_buffers[t.pci].AddPoint(tc, t.rssnr);
                    }
                } 
                tc += 1.0f; 
            }
            
            insertToDatabase(db_con, nd);
            std::cout << "Lat: " << nd.lat 
                      << ", Lon: " << nd.lon << ", Towers: " << nd.towers.size() << std::endl;

            sock.send(zmq::buffer("OK"), zmq::send_flags::none);
            
        } catch (...) { 
            sock.send(zmq::buffer("ERR"), zmq::send_flags::none); 
        }
    }
}