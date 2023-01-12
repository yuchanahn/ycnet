cmake_minimum_required(VERSION 3.24)

project(ycnet
    LANGUAGES CXX)

# get dependencies using CPM
include(cmake/CPM.cmake)
CPMAddPackage(
  NAME ycnet_core
  GITHUB_REPOSITORY yuchanahn/ycnet_core
  GIT_TAG HEAD
)
if (ycnet_core_ADDED)
  add_library(ycnet_core INTERFACE IMPORTED)
  target_include_directories(
  ycnet_core 
  INTERFACE ${ycnet_core_SOURCE_DIR}
  INTERFACE ${ycnet_core_SOURCE_DIR}/inc
  )
endif()
CPMAddPackage(
  NAME yc_framework
  GITHUB_REPOSITORY yuchanahn/yc_framework
  GIT_TAG HEAD
)
if (yc_framework_ADDED)
  add_library(yc_framework INTERFACE IMPORTED)
  target_include_directories(
  yc_framework
  INTERFACE ${yc_framework_SOURCE_DIR}
  INTERFACE ${yc_framework_SOURCE_DIR}/packet
  INTERFACE ${yc_framework_SOURCE_DIR}/thread
  )
endif()

# get all cpp source files
file(GLOB_RECURSE SRC_FILES src/*.cpp)
add_executable(${PROJECT_NAME}
  ${SRC_FILES})
  
target_compile_options(${PROJECT_NAME}
  PRIVATE
    "/std:c++latest")
    
target_link_libraries(${PROJECT_NAME}
  yc_framework
  ycnet_core)
  
target_include_directories(${PROJECT_NAME}
  PRIVATE
  ${PROJECT_SOURCE_DIR}/simple_packet)