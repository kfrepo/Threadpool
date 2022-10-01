cmake_minimum_required(VERSION 3.21)
project(ThreadPoolDemo)

set(CMAKE_CXX_STANDARD 14)

add_executable(threadpool
        ThreadPoolTest.cpp threadpool.h)
