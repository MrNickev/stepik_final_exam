cmake_minimum_required(VERSION 3.19)
project(final)

set(CMAKE_CXX_STANDARD 11)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREAD ON)
find_package(Boost 1.74.0 REQUIRED COMPONENTS system thread)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
endif(Boost_FOUND)

set(USED_LIBS ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} pthread)

add_executable(final main.cpp)
target_link_libraries(final ${USED_LIBS})