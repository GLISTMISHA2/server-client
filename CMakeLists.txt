cmake_minimum_required(VERSION 3.14)
project(backend_notes)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
include_directories(${THIRD_PARTY_DIR}/stb)


find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Ищем ZeroMQ
pkg_check_modules(ZMQ REQUIRED libzmq)

# Ищем PostgreSQL (libpq)
pkg_check_modules(POSTGRES REQUIRED libpq)

# Ищем nlohmann_json
find_path(NLOHMANN_JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
    PATHS /usr/include /usr/local/include /usr/include/nlohmann
)
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    pkg_check_modules(PC_NLOHMANN_JSON QUIET nlohmann_json)
    find_path(NLOHMANN_JSON_INCLUDE_DIR
        NAMES nlohmann/json.hpp
        PATHS ${PC_NLOHMANN_JSON_INCLUDE_DIRS}
    )
endif()

# --- Сборка сторонних библиотек (3rd party) ---
add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${THIRD_PARTY_DIR}/imgui
    ${THIRD_PARTY_DIR}/imgui/backends
    ${SDL2_INCLUDE_DIRS}  # <-- Добавили эту строчку, чтобы ImGui увидел SDL.h
)
target_compile_options(imgui PRIVATE -fPIC)

add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PRIVATE imgui)
target_compile_options(implot PRIVATE -fPIC)

# --- Сборка основного исполняемого файла ---
# КРИТИЧЕСКИ ВАЖНО: Перечисляем ВСЕ файлы реализации (*.cpp) твоего проекта
add_executable(main 
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/database.cpp
    ${SRC_DIR}/gui.cpp
    ${SRC_DIR}/map.cpp
    ${SRC_DIR}/parser.cpp
    ${SRC_DIR}/server.cpp
    ${SRC_DIR}/heatmap.cpp
)

# Связываем таргет main с библиотеками (добавили ${POSTGRES_LIBRARIES} и curl)
target_link_libraries(main PRIVATE
    imgui
    implot
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${ZMQ_LIBRARIES}
    ${POSTGRES_LIBRARIES}
    curl
    Threads::Threads
)

if (UNIX AND NOT APPLE)
    target_link_libraries(main PRIVATE rt dl)
endif()

# Указываем, где искать заголовочные файлы (.h)
target_include_directories(main PRIVATE
    ${INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIRS}
    ${POSTGRES_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIRS}
)

target_compile_definitions(main PRIVATE
    _GLIBCXX_USE_CXX11_ABI=1
)