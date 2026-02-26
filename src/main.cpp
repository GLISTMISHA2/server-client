#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <mutex>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

using json = nlohmann::json;

struct LocationData {
    float latitude = 0.0f;
    float longitude = 0.0f;
    float altitude = 0.0f;
    std::string time = "Нет данных";
    std::mutex mutex;
};

struct LocationHistory {
    std::vector<float> latitudes;
    std::vector<float> longitudes;
    std::vector<float> altitudes;
    std::vector<std::string> times;
    std::mutex mutex;
};

LocationData g_locationData;
LocationHistory g_locationHistory;

void saveToJsonFile(float lat, float lon, float alt, const std::string& time, int counter) {
    try {
        json j;
        j["counter"] = counter;
        j["latitude"] = lat;
        j["longitude"] = lon;
        j["altitude"] = alt;
        j["time"] = time;

        const std::string filename = "location_data.json";

        json root;
        std::ifstream inputFile(filename);

        if (inputFile.good()) {
            try {
                inputFile >> root;
                inputFile.close();
            } catch (...) {
                root = json::array();
            }
        } else {
            root = json::array();
        }

        if (!root.is_array()) {
            root = json::array();
        }
        
        root.push_back(j);
        std::ofstream outputFile(filename);
        outputFile << root.dump(4);
        outputFile.close();
    } catch (const std::exception& e) {
        std::cerr << "Ошибка сохранения файла: " << e.what() << std::endl;
    }
}

void run_gui(LocationData *loc) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow(
        "Location Server",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        400, 300,
        SDL_WINDOW_OPENGL
    );
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    
    // Инициализация ImGui
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    
    // Настройка бэкендов
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");
    
    bool running = true;
    
    while (running) {
        // Обработка событий
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        ImGui::Begin("Location Info");
        
        float lat, lon, alt;
        std::string time;
        {
            std::lock_guard<std::mutex> lock(loc->mutex);
            lat = loc->latitude;
            lon = loc->longitude;
            alt = loc->altitude;
            time = loc->time;
        }
        
        ImGui::Text("Latitude:  %.6f", lat);
        ImGui::Text("Longitude: %.6f", lon);
        ImGui::Text("Altitude:  %.2f m", alt);
        ImGui::Text("Last update: %s", time.c_str());
    
        if (lat != 0.0f) {
            ImGui::Text("Status: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0,1,0,1), "Active");
        } else {
            ImGui::Text("Status: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,0,0,1), "Waiting for data...");
        }
        
        ImGui::End();
        
        ImGui::Render();
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        SDL_GL_SwapWindow(window);
        SDL_Delay(16); 
    }
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void run_server() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);

    try {
        socket.bind("tcp://*:5555");
        std::cout << "Сервер запущен на порту 5555..." << std::endl;
        std::cout << "IP пользователя" << std::flush;

        int counter = 0;

        while (true) {
            try {
                zmq::message_t request;
                socket.set(zmq::sockopt::rcvtimeo, 1000);

                if (socket.recv(request, zmq::recv_flags::none)) {
                    std::string received(static_cast<char*>(request.data()), request.size());
                    std::cout << "Получен JSON: " << received << std::endl;

                    try {
                        json j = json::parse(received);

                        if (j.contains("locations") && j["locations"].is_array()) {
                            auto& locations = j["locations"];
                            if (!locations.empty()) {
                                auto& loc = locations[0];
                                float lat = loc.value("lat", 0.0);
                                float lon = loc.value("lon", 0.0);
                                float alt = loc.value("alt", 0.0);

                                long long time_milliseconds = 0;
                                if (loc.contains("time")) {
                                    if (loc["time"].is_string()) {
                                        std::string time_str = loc["time"];
                                        time_milliseconds = std::stoll(time_str);
                                    } else {
                                        time_milliseconds = loc.value("time", 0LL);
                                    }
                                }

                                std::time_t time_seconds = static_cast<std::time_t>(time_milliseconds / 1000);
                                int milliseconds = static_cast<int>(time_milliseconds % 1000);
                                std::stringstream ss;
                                ss << std::put_time(std::localtime(&time_seconds), "%Y-%m-%d %H:%M:%S");
                                ss << "." << std::setfill('0') << std::setw(3) << milliseconds;
                                std::string formatted_time = ss.str();

                                {
                                    std::lock_guard<std::mutex> lock(g_locationData.mutex);
                                    g_locationData.latitude = lat;
                                    g_locationData.longitude = lon;
                                    g_locationData.altitude = alt;
                                    g_locationData.time = formatted_time;
                                }

                                {
                                    std::lock_guard<std::mutex> lock(g_locationHistory.mutex);
                                    g_locationHistory.latitudes.push_back(lat);
                                    g_locationHistory.longitudes.push_back(lon);
                                    g_locationHistory.altitudes.push_back(alt);
                                    std::stringstream ss_time_only;
                                    ss_time_only << std::put_time(std::localtime(&time_seconds), "%H:%M:%S");
                                    g_locationHistory.times.push_back(ss_time_only.str());
                                }
                                
                                counter++;
                                saveToJsonFile(lat, lon, alt, formatted_time, counter);

                                std::cout << "Данные: lat=" << lat << ", lon=" << lon << ", alt=" << alt << ", time=" << formatted_time << std::endl;
                            }
                        }

                        std::string response = "OK";
                        zmq::message_t reply(response.size());
                        memcpy(reply.data(), response.c_str(), response.size());
                        socket.send(reply, zmq::send_flags::none);

                    } catch (const json::parse_error& e) {
                        std::cerr << "Ошибка обработки JSON: " << e.what() << std::endl;
                        std::string response = "неправильный json";
                        zmq::message_t reply(response.size());
                        memcpy(reply.data(), response.c_str(), response.size());
                        socket.send(reply, zmq::send_flags::none);
                    }
                }
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {
                    std::cerr << "Ошибка zmq: " << e.what() << std::endl;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка сервера: " << e.what() << std::endl;
    }
    
    socket.close();
    context.close();
}

int main(int argc, char *argv[]) {
    std::thread gui_thread(run_gui, &g_locationData);
    std::thread server_thread(run_server);

    gui_thread.join();
    server_thread.join();

}