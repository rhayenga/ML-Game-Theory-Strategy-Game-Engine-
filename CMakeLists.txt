cmake_minimum_required(VERSION 3.20)
project(catan_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(catan
  src/topology.cpp
  src/board.cpp
  src/state.cpp
  src/rules.cpp
  src/eval.cpp
  src/mcts.cpp
  src/train.cpp
  src/json_api.cpp
  src/strategy.cpp
  src/visits.cpp
)
target_include_directories(catan PUBLIC include)
target_compile_options(catan PRIVATE -O3 -Wall -Wextra)

add_executable(catan_advise apps/advise.cpp)
target_link_libraries(catan_advise PRIVATE catan)

add_executable(catan_train apps/train.cpp)
target_link_libraries(catan_train PRIVATE catan)

add_executable(catan_bench apps/bench.cpp)
target_link_libraries(catan_bench PRIVATE catan)

add_executable(catan_bridge apps/bridge.cpp)
target_link_libraries(catan_bridge PRIVATE catan)
