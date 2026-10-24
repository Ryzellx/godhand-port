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
  src/shim/host_io.cpp
  src/gif/gif.cpp
  src/gs/gs_state.cpp
  src/vk/vk_backend.cpp)
target_include_directories(game_loop_test PRIVATE src/shim src/gif src/gs src/vk)
target_link_libraries(game_loop_test PRIVATE Vulkan::Vulkan)

# 3) M5 shim kernel EE (thread/sema/eventflag/file/gif-hook) — portable std::thread
add_executable(shim_test
  src/shim/shim_test.cpp
  src/shim/ee_kernel.cpp
  src/shim/host_io.cpp)
target_include_directories(shim_test PRIVATE src/shim)
find_package(Threads REQUIRED)
target_link_libraries(shim_test PRIVATE Threads::Threads)

# 4) Phase 7a — bus EE (RAM + register MMIO DMA ch2 GIF) -> parser GIF -> gs state
add_executable(bus_test
  src/shim/bus_test.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp
  src/shim/host_io.cpp
  src/gif/gif.cpp
  src/gs/gs_state.cpp)
target_include_directories(bus_test PRIVATE src/shim src/gif src/gs)
target_link_libraries(bus_test PRIVATE Threads::Threads)

# 4) M7b crt0 test — CPU interpreter menjalankan crt0 ASLI dari ELF game
add_executable(crt0_test
  src/shim/crt0_test.cpp
  src/shim/ee_cpu.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp
  src/shim/host_io.cpp
  src/shim/sif_rpc.cpp
  src/shim/sif_dma.cpp
  src/shim/memcard.cpp
  src/shim/iso9660.cpp
  src/gif/gif.cpp)
target_include_directories(crt0_test PRIVATE src/shim src/gif)
find_package(Threads REQUIRED)
target_link_libraries(crt0_test PRIVATE Threads::Threads)

# 4b) iso_test — pembaca ISO9660 mentah (CDVD backend), dibandingkan dengan hasil extract
add_executable(iso_test
  src/shim/iso_test.cpp
  src/shim/iso9660.cpp)
target_include_directories(iso_test PRIVATE src/shim)

# 5) M7b unit probe — semantik branch/delay-slot/jal-jr kanonik MIPS + BTB census
add_executable(branch_test
  src/shim/branch_probe.cpp
  src/shim/ee_cpu.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp
  src/shim/host_io.cpp)
target_include_directories(branch_test PRIVATE src/shim)
target_link_libraries(branch_test PRIVATE Threads::Threads)

# direct_render: render model Gene langsung ke Vulkan (bypass GIF)
add_executable(direct_render
  src/slice/direct_render.cpp
  src/vk/vk_backend.cpp)
target_include_directories(direct_render PRIVATE src/vk)
target_link_libraries(direct_render PRIVATE Vulkan::Vulkan)

# rotate_render: model Gene berputar 360 derajat
add_executable(rotate_render
  src/slice/rotate_render.cpp
  src/vk/vk_backend.cpp)
target_include_directories(rotate_render PRIVATE src/vk)
target_link_libraries(rotate_render PRIVATE Vulkan::Vulkan)

# pl00_v2: GIF pipeline fix — semua vertex dalam satu tag
add_executable(pl00_v2
  src/slice/pl00_v2.cpp
  src/gif/gif.cpp
  src/gs/gs_state.cpp
  src/vk/vk_backend.cpp)
target_include_directories(pl00_v2 PRIVATE src/gif src/gs src/vk)
target_link_libraries(pl00_v2 PRIVATE Vulkan::Vulkan)

# textured_render: model Gene dengan texture dari disc
add_executable(textured_render
  src/slice/textured_render.cpp
  src/vk/vk_backend.cpp)
target_include_directories(textured_render PRIVATE src/vk)
target_link_libraries(textured_render PRIVATE Vulkan::Vulkan)

# game_demo: playable demo dengan interactive controls
add_executable(game_demo
  src/slice/game_demo.cpp
  src/vk/vk_backend.cpp)
target_include_directories(game_demo PRIVATE src/vk)
target_link_libraries(game_demo PRIVATE Vulkan::Vulkan)

# Phase 8 — host filesystem + SIF RPC backend (ganti IOP)
add_executable(sif_test
  src/shim/sif_test.cpp
  src/shim/sif_rpc.cpp
  src/shim/host_io.cpp
  src/shim/memcard.cpp
  src/shim/iso9660.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp)   # cdvd Read_EE menulis langsung ke RAM EE
target_include_directories(sif_test PRIVATE src/shim)

# adx_test: decoder CRI ADX vs referensi ffmpeg (bit-exact <=1 LSB)
add_executable(adx_test
  src/shim/adx_test.cpp
  src/shim/adx.cpp)
target_include_directories(adx_test PRIVATE src/shim)

# sif_dma_test: SifSetDma (syscall 119) -> sifcmd/sifrpc -> backend host (bukti wiring)
add_executable(sif_dma_test
  src/shim/sif_dma_test.cpp
  src/shim/sif_dma.cpp
  src/shim/sif_rpc.cpp
  src/shim/host_io.cpp
  src/shim/memcard.cpp
  src/shim/iso9660.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp
  src/shim/ee_cpu.cpp)   # eecpu::request_callback (end-func RPC, Phase 9)
target_include_directories(sif_dma_test PRIVATE src/shim)
target_link_libraries(sif_dma_test PRIVATE Threads::Threads)

# mmi_test: unit test MMI R5900 (opcode 0x1C) — mengunci fix stub 2026-10-24
add_executable(mmi_test
  src/shim/mmi_test.cpp
  src/shim/ee_cpu.cpp
  src/shim/ee_bus.cpp
  src/shim/ee_kernel.cpp
  src/shim/host_io.cpp)
target_include_directories(mmi_test PRIVATE src/shim)
target_link_libraries(mmi_test PRIVATE Threads::Threads)
