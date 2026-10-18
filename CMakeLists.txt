cmake_minimum_required(VERSION 3.16)
project(godhand_port CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# 1) Unit test parser GIF — tanpa GPU
add_executable(gif_test src/gif/gif_test.cpp src/gif/gif.cpp)
target_include_directories(gif_test PRIVATE src/gif)

# 2) M4 vertical slice: packet GIF -> GS state -> Vulkan offscreen -> verifikasi piksel
find_package(Vulkan REQUIRED)
add_executable(godhand_slice
  src/slice/main.cpp
  src/gif/gif.cpp
  src/gs/gs_state.cpp
  src/vk/vk_backend.cpp)
target_include_directories(godhand_slice PRIVATE src/gif src/gs src/vk)
target_link_libraries(godhand_slice PRIVATE Vulkan::Vulkan)

# 3) M5 shim kernel EE (thread/sema/eventflag/file/gif-hook) — portable std::thread
add_executable(shim_test
  src/shim/shim_test.cpp
  src/shim/ee_kernel.cpp)
target_include_directories(shim_test PRIVATE src/shim)
find_package(Threads REQUIRED)
target_link_libraries(shim_test PRIVATE Threads::Threads)

# 4) Phase 7a — bus EE (RAM + register MMIO DMA ch2 GIF) -> parser GIF -> gs state
add_executable(bus_test
  src/shim/bus_test.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp
  src/gif/gif.cpp
  src/gs/gs_state.cpp)
target_include_directories(bus_test PRIVATE src/shim src/gif src/gs)
target_link_libraries(bus_test PRIVATE Threads::Threads)
