# MIT License

# Copyright (c) 2022 Duke University. All rights reserved.

# See LICENSE for license information
cmake_minimum_required(VERSION 3.16)

# set the project name
project(PerfIsolation)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)


find_package(PkgConfig REQUIRED)


# pkg_check_modules(DPDK "libdpdk")
# if (DPDK_FOUND)
#   add_definitions(${DPDK_CFLAGS})
#   set(MYDPDK_LIBRARIES -Wl,--whole-archive ${DPDK_LIBRARIES} -lpthread -lnuma -ldl -Wl,--no-whole-archive)
#   message (STATUS "${MYDPDK_LIBRARIES}")
#   include_directories(${DPDK_INCLUDE_DIR})
#   link_libraries(${MYDPDK_LIBRARIES})
#   add_definitions(-DHAVE_DPDK)
# else()
#   message(FATAL_ERROR "dpdk not found via pkg-config")
# endif(DPDK_FOUND)

pkg_check_modules(GLOG "libglog")
if (GLOG_FOUND)
  add_definitions(${GLOG_CFLAGS})
  include_directories(${GLOG_INCLUDE_DIR})
  link_libraries(${GLOG_LIBRARIES})
  add_definitions(-DHAVE_GLOG)
else()
  message(FATAL_ERROR "glog not found via pkg-config")
endif(GLOG_FOUND)


pkg_check_modules(GFLAGS "gflags")
if (GFLAGS_FOUND)
  add_definitions(${GFLAGS_CFLAGS})
  include_directories(${GFLAGS_INCLUDE_DIR})
  link_libraries(${GFLAGS_LIBRARIES})
  add_definitions(-DHAVE_GFLAGS)
else()
  message(FATAL_ERROR "gflags not found via pkg-config")
endif(GFLAGS_FOUND)

pkg_check_modules(VERBS "ibverbs")
if (VERBS_FOUND)
  message(STATUS "ibverbs found via pkg-config")
  add_definitions($(VERBS_CFLAGS))
  include_directories(${VERBS_INCLUDE_DIR})
  link_libraries(${VERBS_LIBRARIES})
  add_definitions(-DHAVE_VERBS)
else(VERBS_FOUND)
  message(STATUS "ibverbs not found via pkg-config. Try find libraries and find path...")
  find_library(VERBS_LIB 
  NAMES ibverbs)
  if (NOT VERBS_LIB)
    message(FATAL_ERROR "verbs not found via find_library")
  else()
    message(STATUS "${VERBS_LIB}")
    link_libraries(${VERBS_LIB})
  endif() 
  find_path(VERBS_INC_DIR
  NAMES infiniband/verbs.h)
  if (NOT VERBS_INC_DIR)
    message(FATAL_ERROR "verbs not found via find_path")
  else()
    include_directories(${VERBS_INC_DIR})
  endif()
endif()

# if (DEBUG) 
#   add_definitions("-DDEBUG -g -O0")
# else()
#   add_definitions("-O2")
# endif(DEBUG)

include_directories("tests/")
include_directories("include/dpdk-backend/")
include_directories("include/rdma-backend/")
include_directories("include/app/")

set(MY_LIBRARIES -lpthread -lnuma -ldl)
link_libraries(${MY_LIBRARIES})

set(DPDK_BACKEND
  src/dpdk-backend/dpdk_port.cpp
  src/dpdk-backend/dpdk_engine.cpp
)

set(DPDK_APP_SOURCES 
 src/app/dpdk_main.cpp
)

set(RDMA_BACKEND 
  src/rdma-backend/rdma_context.cpp
  src/rdma-backend/rdma_helper.cpp
  src/rdma-backend/rdma_endpoint.cpp
  src/rdma-backend/rdma_memory.cpp
)

set(RDMA_APP_SOURCES
  src/app/rdma_main.cpp
)

set (RDMA_CTRL_TEST_SOURCES
  src/app/rdma_ctrl.cpp
)

add_executable(RdmaEngine ${RDMA_APP_SOURCES} ${RDMA_BACKEND})
add_executable(RdmaCtrlTest ${RDMA_CTRL_TEST_SOURCES})
# add_executable(DpdkEngine ${DPDK_APP_SOURCES} ${DPDK_BACKEND})
# add_executable(Test tests/test.cpp)

install(TARGETS RdmaEngine DESTINATION bin)
install(TARGETS RdmaCtrlTest DESTINATION bin)
install(FILES rdma_monitor.py DESTINATION /tmp)
