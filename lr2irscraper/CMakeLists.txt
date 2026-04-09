cmake_minimum_required(VERSION 3.29)
project(lr2irscraper LANGUAGES CXX)

find_package(LibXml2 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(cpr REQUIRED)

if(NOT TARGET SQLite3::SQLite3) # CMake <4
  add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

add_executable(lr2irscraper ./lr2irscraper.cpp)
set_target_properties(lr2irscraper PROPERTIES CXX_STANDARD 23)
target_link_libraries(lr2irscraper PUBLIC cpr::cpr SQLite3::SQLite3 LibXml2::LibXml2)
