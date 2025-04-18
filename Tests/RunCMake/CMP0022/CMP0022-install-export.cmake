
enable_language(CXX)

add_library(cmp0022NEW SHARED empty_vs6_1.cpp)
add_library(testLib SHARED empty_vs6_2.cpp)

set_property(TARGET cmp0022NEW APPEND PROPERTY LINK_INTERFACE_LIBRARIES testLib)

install(TARGETS cmp0022NEW testLib EXPORT exp DESTINATION lib)
install(EXPORT exp FILE expTargets.cmake DESTINATION lib/cmake/exp)
