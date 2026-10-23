cmake_minimum_required(VERSION 3.16)
project(RustClone CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Статичний CRT: exe не потребує додаткових DLL
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# raylib 5.5 (збирається статично й підтягується автоматично)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

set(SRC main.cpp)
# Іконка: поклади icon.ico поруч з main.cpp
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
  file(WRITE "${CMAKE_BINARY_DIR}/icon.rc" "1 ICON \"${CMAKE_SOURCE_DIR}/icon.ico\"\n")
  list(APPEND SRC "${CMAKE_BINARY_DIR}/icon.rc")
endif()

add_executable(RustClone ${SRC})
target_link_libraries(RustClone PRIVATE raylib)

if(WIN32)
  # без чорного вікна консолі
  if(MSVC)
    set_target_properties(RustClone PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    target_compile_options(RustClone PRIVATE /utf-8)
  else()
    target_link_options(RustClone PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
  endif()
endif()
