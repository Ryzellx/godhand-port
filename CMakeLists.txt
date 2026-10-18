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

# pl00_render: render model Gene dari disc -> GIF -> GS -> Vulkan -> PPM
add_executable(pl00_render
  src/slice/pl00_render.cpp
  src/gif/gif.cpp
  src/gs/gs_state.cpp
  src/vk/vk_backend.cpp)
target_include_directories(pl00_render PRIVATE src/gif src/gs src/vk)
target_link_libraries(pl00_render PRIVATE Vulkan::Vulkan)

# game_loop_test: CPU interpreter -> DMA ch2 -> GIF -> GS -> Vulkan (integrasi penuh)
add_executable(game_loop_test
  src/shim/game_loop_test.cpp
  src/shim/ee_cpu.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp
  src/gif/gif.cpp
  src/gs/gs_state.cpp
  src/vk/vk_backend.cpp)
target_include_directories(game_loop_test PRIVATE src/shim src/gif src/gs src/vk)
target_link_libraries(game_loop_test PRIVATE Vulkan::Vulkan)

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

# 4) M7b crt0 test — CPU interpreter menjalankan crt0 ASLI dari ELF game
add_executable(crt0_test
  src/shim/crt0_test.cpp
  src/shim/ee_cpu.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp)
target_include_directories(crt0_test PRIVATE src/shim)
find_package(Threads REQUIRED)
target_link_libraries(crt0_test PRIVATE Threads::Threads)

# 5) M7b unit probe — semantik branch/delay-slot/jal-jr kanonik MIPS + BTB census
add_executable(branch_test
  src/shim/branch_probe.cpp
  src/shim/ee_cpu.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp)
target_include_directories(branch_test PRIVATE src/shim)
target_link_libraries(branch_test PRIVATE Threads::Threads)

# direct_render: render model Gene langsung ke Vulkan (bypass GIF)
add_executable(direct_render
  src/slice/direct_render.cpp
  src/vk/vk_backend.cpp)
target_include_directories(direct_render PRIVATE src/vk)
target_link_libraries(direct_render PRIVATE Vulkan::Vulkan)
