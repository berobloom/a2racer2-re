cmake_minimum_required(VERSION 3.16)
project(a2racer2 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Format/asset core: no SDL, no GL, so it can be tested headless.
add_library(a2core STATIC
  src/a2_io.c
  src/cars.c
  src/archive.c
  src/mesh.c
  src/track.c
)
target_include_directories(a2core PUBLIC src)

add_executable(a2_dump src/a2_dump.c)
target_link_libraries(a2_dump PRIVATE a2core)

# Dev tool: decode a Smacker video with the menu's decoder (frames + audio).
add_executable(smkdump src/menu/smkdump.c src/menu/msmacker.c)
target_link_libraries(smkdump PRIVATE a2core)

# The viewer needs SDL2 + OpenGL; skip it (with a note) when they are missing
# so the core and its tests still build.
find_package(SDL2 QUIET)
find_package(OpenGL QUIET)
if(SDL2_FOUND AND OPENGL_FOUND)
  add_executable(a2view
    src/main.c
    src/audio.c
    src/drive.c
    src/smoke.c
    src/skid.c
    src/paths.c
    src/anims.c
    src/font.c
    src/font3d.c
    src/gfx.c
    src/lang.c
    src/handling.c
    src/racers.c
    src/wheels.c
    src/contact.c
    src/sky.c
    src/trackinfo.c
    src/results.c
    src/sprites.c
    src/cops.c
    src/traffic.c
    src/ai_lane.c
    src/ai_car.c
    src/ai_world.c
    src/render.c
    src/pcx.c
    src/tga.c
  )
  target_link_libraries(a2view PRIVATE a2core ${OPENGL_gl_LIBRARY} m)
  if(TARGET SDL2::SDL2)
    target_link_libraries(a2view PRIVATE SDL2::SDL2)
  else()
    target_include_directories(a2view PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(a2view PRIVATE ${SDL2_LIBRARIES})
  endif()
  target_include_directories(a2view PRIVATE ${OPENGL_INCLUDE_DIR})

  # The front end (menu.dat): a 640x480 8-bit canvas through SDL's renderer.
  add_executable(a2menu
    src/menu/a2menu.c
    src/menu/m_screens.c
    src/menu/m_garage.c
    src/menu/m_showroom.c
    src/menu/m_standings.c
    src/menu/m_options.c
    src/menu/m_controls.c
    src/menu/m_newplayer.c
    src/menu/maudio.c
    src/menu/msmacker.c
    src/menu/mui.c
    src/menu/massets.c
    src/menu/mcareer.c
    src/menu/mdata.c
    src/menu/mgfx.c
    src/trackinfo.c
    src/pcx.c
    src/tga.c
  )
  target_link_libraries(a2menu PRIVATE a2core m)
  if(TARGET SDL2::SDL2)
    target_link_libraries(a2menu PRIVATE SDL2::SDL2)
  else()
    target_include_directories(a2menu PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(a2menu PRIVATE ${SDL2_LIBRARIES})
  endif()
else()
  message(STATUS "SDL2 or OpenGL not found -- building a2core/a2_dump only")
endif()
