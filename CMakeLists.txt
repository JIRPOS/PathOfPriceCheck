cmake_minimum_required(VERSION 3.20)

# MAJOR.MINOR comes from the VERSION file; BUILD is injected by CI (-DAPP_BUILD=<run number>).
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" VERSION_BASE)
if(NOT DEFINED APP_BUILD)
  set(APP_BUILD 0)
endif()
set(APP_VERSION "${VERSION_BASE}.${APP_BUILD}")

project(PathOfPriceCheck VERSION ${APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(PathOfPriceCheck src/main.cpp)
target_compile_definitions(PathOfPriceCheck PRIVATE APP_VERSION="${APP_VERSION}")

enable_testing()
add_executable(smoke_test tests/smoke_test.cpp)
add_test(NAME smoke COMMAND smoke_test)
