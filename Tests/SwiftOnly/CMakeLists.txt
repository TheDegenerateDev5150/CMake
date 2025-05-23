cmake_minimum_required(VERSION 3.10)
if(POLICY CMP0126)
  cmake_policy(SET CMP0126 NEW)
endif()
if(POLICY CMP0157)
  cmake_policy(SET CMP0157 NEW)
endif()
if(POLICY CMP0195)
  cmake_policy(SET CMP0195 NEW)
  set(Swift_NESTED_MODULE TRUE)
endif()

# NOTE: Force the Release mode configuration as there are some issues with the
# debug information handling on macOS on certain Xcode builds.
if(NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
endif()

# NOTE: enable shared libraries by default.  Older Xcode releases do not play
# well with static libraries, and Windows does not currently support static
# libraries in Swift.
set(BUILD_SHARED_LIBS YES)

project(SwiftOnly Swift)

if(NOT XCODE_VERSION VERSION_LESS 10.2)
  set(CMAKE_Swift_LANGUAGE_VERSION 5.0)
elseif(NOT XCODE_VERSION VERSION_LESS 8.0)
  set(CMAKE_Swift_LANGUAGE_VERSION 3.0)
endif()

add_subdirectory(SubA)
add_subdirectory(SubB)

add_subdirectory(SubC)
add_subdirectory(SubD)
add_subdirectory(SubE)

add_subdirectory("Sub Space")

set(CMAKE_Swift_MODULE_DIRECTORY
  "${CMAKE_BINARY_DIR}/modules/$<IF:$<CONFIG:Debug>,debug/,release/>swift")

add_executable(SwiftOnly main.swift)
target_compile_definitions(SwiftOnly PRIVATE SWIFTONLY)

add_library(L L.swift)

add_library(M M.swift)
target_link_libraries(M PUBLIC
  L)

add_library(N N.swift)
target_link_libraries(N PUBLIC
  M)

# FIXME(#25989): The Xcode generator doesn't respect CMAKE_Swift_MODULE_DIRECTORY.
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
  if(Swift_NESTED_MODULE)
    # Swift module is a directory, grab the binary swiftmodule file
    set(ModulePath "${CMAKE_Swift_MODULE_DIRECTORY}/M.swiftmodule/${CMAKE_Swift_MODULE_TRIPLE}.swiftmodule")
  else()
    # Swift modules are files
    set(ModulePath "${CMAKE_Swift_MODULE_DIRECTORY}/M.swiftmodule")
  endif()
  add_custom_command(TARGET M
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E compare_files
    "${ModulePath}"
    "${ModulePath}"
    COMMENT "check that .swiftmodule files are generated in CMAKE_Swift_MODULE_DIRECTORY"
    VERBATIM)
endif()

if(NOT XCODE_VERSION OR XCODE_VERSION VERSION_GREATER_EQUAL 9.0)
  # TODO: Add a wholemodule object-library test once that is working
  add_library(O OBJECT O.swift L.swift)
  target_link_libraries(N PUBLIC O)
  set_target_properties(O PROPERTIES Swift_COMPILATION_MODE "incremental")
endif()

add_library(P L.swift)
add_dependencies(P SwiftOnly)

# Dummy to make sure generation works with such targets.
add_library(SwiftIface INTERFACE)
target_link_libraries(SwiftOnly PRIVATE SwiftIface)

add_subdirectory("SwiftPlugin")

function(test_cmp0157_default mode)
  if(POLICY CMP0157)
    cmake_policy(GET CMP0157 cmp0157_wmo)
    if(cmp0157_wmo STREQUAL "NEW")
      set(CMAKE_Swift_COMPILATION_MODE "${mode}")
      add_executable(hi_${mode} main.swift)
      get_target_property(${mode}_swift_comp_mode hi_${mode} "Swift_COMPILATION_MODE")
      if(NOT ${mode}_swift_comp_mode STREQUAL ${mode})
        message(SEND_ERROR "expected ${mode} -- found ${${mode}_swift_comp_mode}")
      endif()
    endif()
  endif()
endfunction()

test_cmp0157_default("wholemodule")
test_cmp0157_default("incremental")
test_cmp0157_default("singlefile")
