cmake_minimum_required(VERSION 3.10)
# set the project name
project(rdmaperf-iso C)

set(HEADER_DIR  /home/yjjeon/rdmaperf-iso/rdma-core-51mlnx1/build/include)
set(LIB_DIR  /home/yjjeon/rdmaperf-iso/rdma-core-51mlnx1/build/lib)
set(SRCS monitor.c misc.c)


include_directories(${HEADER_DIR})
link_directories(${LIB_DIR})

# add the executable
add_executable(monitor ${SRCS})
target_link_libraries(monitor pthread rt ibverbs)


