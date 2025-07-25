if("$ENV{CMAKE_CI_BUILD_NAME}" MATCHES "(^|_)gnu(_|$)")
  set(CMake_TEST_C_STANDARDS "90;99;11;17;23" CACHE STRING "")
  set(CMake_TEST_CXX_STANDARDS "98;11;14;17;20;23;26" CACHE STRING "")
else()
  # Testing for clang-cl.
  set(CMake_TEST_C_STANDARDS "90;99;11;17;23" CACHE STRING "")
  set(CMake_TEST_CXX_STANDARDS "98;11;14;17;20;23" CACHE STRING "")
endif()

set(CMake_TEST_FindOpenMP "ON" CACHE BOOL "")
set(CMake_TEST_FindOpenMP_C "ON" CACHE BOOL "")
set(CMake_TEST_FindOpenMP_CXX "ON" CACHE BOOL "")
set(CMake_TEST_FindOpenMP_Fortran "OFF" CACHE BOOL "")
set(CMake_TEST_Java OFF CACHE BOOL "")

set(configure_no_sccache 1)

include("${CMAKE_CURRENT_LIST_DIR}/configure_external_test.cmake")
