set(CMake_TEST_C_STANDARDS "90;99;11;17;23" CACHE STRING "")
set(CMake_TEST_CXX_STANDARDS "98;11;14;17;20;23" CACHE STRING "")

if (NOT "$ENV{CMAKE_CI_NIGHTLY}" STREQUAL "")
  set(CMake_TEST_CPACK_INNOSETUP "ON" CACHE STRING "")
  set(CMake_TEST_CPACK_NUGET "ON" CACHE STRING "")
  set(CMake_TEST_IAR_TOOLCHAINS "$ENV{CI_PROJECT_DIR}/.gitlab/iar" CACHE PATH "")
  set(CMake_TEST_ISPC "ON" CACHE STRING "")
  set(CMake_TEST_Swift "ON" CACHE STRING "")
endif()

set(CMake_TEST_TLS_VERIFY_URL "https://gitlab.kitware.com" CACHE STRING "")
set(CMake_TEST_TLS_VERIFY_URL_BAD "https://badtls-expired.kitware.com" CACHE STRING "")
set(CMake_TEST_TLS_VERSION "1.2" CACHE STRING "")
set(CMake_TEST_TLS_VERSION_URL_BAD "https://badtls-v1-1.kitware.com:8011" CACHE STRING "")

# Release flags without -DNDEBUG so we get assertions.
set(CMAKE_C_FLAGS_RELEASE "-O2 -Ob2" CACHE STRING "")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -Ob2" CACHE STRING "")

include("${CMAKE_CURRENT_LIST_DIR}/configure_windows_msvc_cxx_modules_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/configure_windows_wix_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/configure_windows_vs_common_ninja.cmake")
