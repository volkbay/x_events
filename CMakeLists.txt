#################################################################################
# User build settings

set(DUAL_THREAD true) # Set true to process image and inertial data on different
		      # threads
set(VERBOSE true) # Set false to disable all publishing and standard output
				  # stream, except pose at update rate. That will improve runtime.
set(TIMING false) # Set true to enable timers
set(PROFILING false)  # Set true to disable compiler flags which are not
		      # compatible with Callgrind profiling tool.
set(UNIT_TESTS false) # Set true to enable unit tests

#################################################################################

cmake_minimum_required(VERSION 2.8.3)
cmake_policy(SET CMP0048 NEW)  # avoids CMAKE_PROJECT_VERSION warning
project(x)

set(CMAKE_BUILD_TYPE Release)

# Set definitions
if(DUAL_THREAD)
  add_definitions(-DDUAL_THREAD)
endif()
if(VERBOSE)
  add_definitions(-DVERBOSE)
endif()
if(TIMING)
  add_definitions(-DTIMING)
endif()
if(UNIT_TESTS)
  add_definitions(-DRUN_UNIT_TESTS)
endif()
if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions(-DDEBUG -DDEBUGMSF)
elseif (CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  # Enable asserts
  add_definitions(-UNDEBUG)
endif()
add_definitions(-D_LINUX -D_REENTRANT)

# Eigen plugin
add_definitions(-DEIGEN_MATRIXBASE_PLUGIN=<x/common/eigen_matrix_base_plugin.h>)

# Look for OpenCV >= 3.3.1
find_package(OpenCV 4 QUIET)
if(NOT ${OpenCV_FOUND})
  find_package(OpenCV 3.3.1 QUIET)
  if(NOT ${OpenCV_FOUND})
    # OpenCV: fallback to opencv3_catkin ros package
    find_package(opencv3_catkin QUIET)
    if(opencv3_catkin_FOUND)
      message("OpenCV3_catkin detected")
      set(OPENCV_PACKAGE "opencv3_catkin")
      set(OpenCV_INCLUDE_DIRS "")
      set(OpenCV_LIBRARIES "")
    else()
      message(FATAL_ERROR "No OpenCV >= 3.3.1 or 4 detected.")
    endif()
  endif()
endif()

find_package(catkin REQUIRED COMPONENTS
  cmake_modules
  ${OPENCV_PACKAGE}
  ceres_catkin
  # eigen_catkin # @volkbay: modified for eigen3v3.4
  glog_catkin
)

find_package(Eigen3 3.4 CONFIG REQUIRED) # @volkbay: modified for eigen3v3.4
set(EIGEN3_INCLUDE_DIR "${Eigen3_DIR}/../../../include/eigen3/") # @volkbay: modified for eigen3v3.4
#message("Eigen version: ${Eigen3_VERSION}")
find_package(easy_profiler_catkin REQUIRED)


# Set build flags, depending on the architecture
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall")

if (CMAKE_BUILD_TYPE MATCHES Debug)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
endif()

if (CMAKE_BUILD_TYPE MATCHES Release)

  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64") # tested on Jetson TX2
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+crypto -mcpu=cortex-a57+crypto -flto -ffast-math -fvect-cost-model=unlimited")
  #elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch32") # uncomment with correct check for Snapdragon Flight Pro
  #  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=softfp -flto -ffast-math -fvect-cost-model=unlimited")
  endif()
  
  if (${PROFILING} MATCHES false)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -funsafe-loop-optimizations -fsee -funroll-loops -fno-math-errno -funsafe-math-optimizations -ffinite-math-only -fno-signed-zeros")
  endif()

endif()

# build with catkin build x --cmake-args -DMY_DEBUG=1
if (MY_DEBUG)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Og")
endif()

## For debugging memory leaks
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Og -fno-omit-frame-pointer -fsanitize=address") # REMOVE ME LATER
#set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -g -Og -fno-omit-frame-pointer -fsanitize=address") # REMOVE ME LATER

# For downstream packages in catkin
set(EIGEN3_INCLUDE_DIRS ${EIGEN3_INCLUDE_DIR})
set(EIGEN3_LIBRARIES ${EIGEN3_LIBRARIES})

# Configure this package
catkin_package(
  DEPENDS EIGEN3
  INCLUDE_DIRS include ${EIGEN3_INCLUDE_DIRS} ${ceres_catkin_INCLUDE_DIRS} ${glog_catkin_INCLUDE_DIRS}
  LIBRARIES x
)

## Package internal and additional locations of header files
## Separating the projects include directory from {catkin_INCLUDE_DIRS}
## allows to tag that with SYSTEM, which disables GCC warnings for
## these (all the ros header, opencv, ..)
include_directories (include)

include_directories (SYSTEM
  ${OpenCV_INCLUDE_DIRS}
  ${catkin_INCLUDE_DIRS}
  # ${EIGEN3_INCLUDE_DIR} # @volkbay: modified for eigen3v3.4
  ${easy_profiler_catkin_INCLUDE_DIRS}
  ${glog_catkin_INCLUDE_DIRS}
)

set (SOURCE
  src/x/ekf/ekf.cpp
  src/x/ekf/propagator.cpp
  src/x/ekf/state.cpp
  src/x/ekf/state_buffer.cpp
  src/x/ekf/updater.cpp
  src/x/vio/vio.cpp
  src/x/vio/vio_updater.cpp
  src/x/vio/state_manager.cpp
  src/x/vio/track_manager.cpp
  src/x/vio/msckf_update.cpp
  src/x/vio/msckf_slam_update.cpp
  src/x/vio/slam_update.cpp
  src/x/vio/range_update.cpp
  src/x/vio/solar_update.cpp
  src/x/vio/tools.cpp
  src/x/vision/utils.cpp
  src/x/vision/camera.cpp
  src/x/vision/feature.cpp
  src/x/vision/tiled_image.cpp
  src/x/vision/timing.cpp
  src/x/vision/tracker.cpp
  src/x/vision/triangulation.cpp
  src/x/eklt/optimizer.cpp
  src/x/eklt/eklt_tracker.cpp
  src/x/eklt/async_feature_interpolator.cpp
  src/x/eklt/viewer.cpp
  src/x/eklt/eklt_vio_updater.cpp
  src/x/eklt/eklt_vio.cpp
  src/x/eklt/types.cpp
  src/x/eklt/utils.cpp
  src/x/events/e_vio.cpp
  src/x/events/event_accumulator.cpp
  src/x/eklt/async_feature_tracker.cpp
  src/x/haste/haste_tracker.cpp
  src/x/haste/haste_vio.cpp
)

add_library (x ${SOURCE})

target_compile_definitions(x PUBLIC -DUSING_EASY_PROFILER)

# Additional libraries to link against
target_link_libraries(x
  ${OpenCV_LIBRARIES}
  ${catkin_LIBRARIES}
  ${easy_profiler_catkin_LIBRARIES}
  Eigen3::Eigen # @volkbay: modified for eigen3v3.4
)
