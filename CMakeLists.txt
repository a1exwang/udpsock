cmake_minimum_required(VERSION 3.10)
project(udpsock)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS system regex program_options)
link_libraries(${Boost_LIBRARIES})

add_executable(udpsock uclient.cpp tun.cpp )
target_link_libraries(udpsock pthread glog)
