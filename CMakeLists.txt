cmake_minimum_required(VERSION 3.29)
project(goodrating LANGUAGES CXX)

find_package(OpenMP COMPONENTS CXX)

add_executable(goodrating ./goodrating.cpp)
set_target_properties(goodrating PROPERTIES CXX_STANDARD 23)
target_link_libraries(goodrating PRIVATE OpenMP::OpenMP_CXX)
