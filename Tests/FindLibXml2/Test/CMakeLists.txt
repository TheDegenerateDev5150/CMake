cmake_minimum_required(VERSION 3.10)
project(TestFindLibXml2 C)
include(CTest)

find_package(LibXml2 REQUIRED)

add_definitions(-DCMAKE_EXPECTED_LibXml2_VERSION="${LibXml2_VERSION}")

add_executable(test_tgt main.c)
target_link_libraries(test_tgt LibXml2::LibXml2)
add_test(NAME test_tgt COMMAND test_tgt)

add_executable(test_var main.c)
target_include_directories(test_var PRIVATE ${LIBXML2_INCLUDE_DIRS})
target_link_libraries(test_var PRIVATE ${LIBXML2_LIBRARIES})
add_test(NAME test_var COMMAND test_var)

add_test(NAME xmllint_tgt COMMAND LibXml2::xmllint --version)

add_test(NAME xmllint_var COMMAND ${LIBXML2_XMLLINT_EXECUTABLE} --version)
