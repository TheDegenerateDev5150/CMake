cmake_policy(SET CMP0074 NEW)
set(PrimaryUnwind_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/UnwindInclude)

set(UNWIND_TARGET UnwindSecondary)
find_package(PrimaryUnwind)
