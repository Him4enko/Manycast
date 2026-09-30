cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(MANYCAST_BUILD_TESTS "Build the unit tests for the OBS independent code" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# Unit tested code without OBS dependencies; also compiled into the plugin
set(MANYCAST_COMMON_SOURCES
    src/common/format.cpp
    src/common/format.hpp
    src/common/snap.cpp
    src/common/snap.hpp
    src/common/stats.cpp
    src/common/stats.hpp
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/common/guard.hpp
    src/common/localization.hpp
    src/common/obs-refs.hpp
    src/common/plugin-state.cpp
    src/common/plugin-state.hpp
    src/core/portrait-canvas.cpp
    src/core/portrait-canvas.hpp
    src/core/config.cpp
    src/core/config.hpp
    src/core/output.cpp
    src/core/output.hpp
    src/core/services.cpp
    src/core/services.hpp
    src/core/destination.hpp
    src/ui/portrait-dock.cpp
    src/ui/portrait-dock.hpp
    src/ui/portrait-preview.cpp
    src/ui/portrait-preview.hpp
    src/ui/destinations-dock.cpp
    src/ui/destinations-dock.hpp
    src/ui/destination-dialog.cpp
    src/ui/destination-dialog.hpp
    ${MANYCAST_COMMON_SOURCES}
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(MANYCAST_BUILD_TESTS)
  enable_testing()

  add_executable(manycast-tests tests/test-main.cpp ${MANYCAST_COMMON_SOURCES})
  target_include_directories(manycast-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_include_directories(manycast-tests SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/doctest")
  target_compile_features(manycast-tests PRIVATE cxx_std_20)
  set_target_properties(
    manycast-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests/$<CONFIG>"
  )

  add_test(NAME manycast-unit COMMAND manycast-tests)
endif()
