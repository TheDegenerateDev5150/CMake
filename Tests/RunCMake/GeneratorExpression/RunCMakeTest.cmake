include(RunCMake)
set(RunCMake_IGNORE_POLICY_VERSION_DEPRECATION ON)

run_cmake(BadIF)
run_cmake(BadCONFIG)
run_cmake(BadOR)
run_cmake(BadAND)
run_cmake(BadNOT)
run_cmake(BadStrEqual)
run_cmake(BadZero)
run_cmake(BadTargetName)
run_cmake(BadTargetTypeInterface)
run_cmake(BadTargetTypeObject)
run_cmake(BadInstallPrefix)
run_cmake(BadSHELL_PATH)
run_cmake(NonValidTarget-C_COMPILER_ID)
run_cmake(NonValidTarget-CXX_COMPILER_ID)
run_cmake(NonValidTarget-Fortran_COMPILER_ID)
run_cmake(NonValidTarget-C_COMPILER_VERSION)
run_cmake(NonValidTarget-CXX_COMPILER_VERSION)
run_cmake(NonValidTarget-Fortran_COMPILER_VERSION)
run_cmake(NonValidTarget-C_COMPILER_FRONTEND_VARIANT)
run_cmake(NonValidTarget-CXX_COMPILER_FRONTEND_VARIANT)
run_cmake(NonValidTarget-Fortran_COMPILER_FRONTEND_VARIANT)
run_cmake(NonValidTarget-TARGET_PROPERTY)
run_cmake(NonValidTarget-TARGET_POLICY)
run_cmake(COMPILE_ONLY-not-compiling)
run_cmake(LINK_ONLY-not-linking)
run_cmake(TARGET_EXISTS-no-arg)
run_cmake(TARGET_EXISTS-empty-arg)
run_cmake(TARGET_EXISTS)
run_cmake(TARGET_EXISTS-not-a-target)
run_cmake(TARGET_NAME_IF_EXISTS-no-arg)
run_cmake(TARGET_NAME_IF_EXISTS-empty-arg)
run_cmake(TARGET_NAME_IF_EXISTS)
run_cmake(TARGET_NAME_IF_EXISTS-not-a-target)
run_cmake(TARGET_NAME_IF_EXISTS-alias-target)
run_cmake(TARGET_NAME_IF_EXISTS-imported-target)
run_cmake(TARGET_NAME_IF_EXISTS-imported-global-target)
run_cmake(REMOVE_DUPLICATES-empty)
run_cmake(REMOVE_DUPLICATES-empty-element)
run_cmake(REMOVE_DUPLICATES-1)
run_cmake(REMOVE_DUPLICATES-2)
run_cmake(REMOVE_DUPLICATES-3)
run_cmake(REMOVE_DUPLICATES-4)
run_cmake(FILTER-empty)
run_cmake(FILTER-InvalidOperator)
run_cmake(FILTER-Exclude)
run_cmake(FILTER-Include)

function(run_cmake_build test)
  set(RunCMake_TEST_BINARY_DIR ${RunCMake_BINARY_DIR}/${test}-build)
  if (RunCMake_GENERATOR_IS_MULTI_CONFIG)
    list(APPEND RunCMake_TEST_OPTIONS -DCMAKE_CONFIGURATION_TYPES=Release)
  else()
    list(APPEND RunCMake_TEST_OPTIONS -DCMAKE_BUILD_TYPE=Release)
  endif()

  run_cmake(${test})

  set(RunCMake_TEST_NO_CLEAN TRUE)
  run_cmake_command(${test}-build ${CMAKE_COMMAND} --build . --config Release)
endfunction()

function(run_linker_genex test lang type)
  set(options_args CHECK_RESULT EXECUTE)
  cmake_parse_arguments(PARSE_ARGV 3 RLG "${options_args}" "" "")

  set(RunCMake_TEST_VARIANT_DESCRIPTION ".${lang}.${type}")
  set(test_name ${test}${RunCMake_TEST_VARIANT_DESCRIPTION})
  set(RunCMake_TEST_BINARY_DIR "${RunCMake_BINARY_DIR}/${test_name}-build")
  if(NOT RunCMake_GENERATOR_IS_MULTI_CONFIG)
    list(APPEND options -DCMAKE_BUILD_TYPE=Release)
  endif()
  if(RLG_CHECK_RESULT)
    set(RunCMake_TEST_EXPECT_RESULT 1)
    file(READ "${RunCMake_SOURCE_DIR}/${test_name}-stderr.txt" RunCMake_TEST_EXPECT_stderr)
  endif()
  run_cmake_with_options(${test} -DLANG=${lang} -DTYPE=${type})
  set(RunCMake_TEST_NO_CLEAN 1)
  unset(RunCMake_TEST_VARIANT_DESCRIPTION)
  if(RLG_EXECUTE)
    run_cmake_command(${test_name}-build "${CMAKE_COMMAND}" --build . --config Release)
    run_cmake_command(${test_name}-run "${CMAKE_CTEST_COMMAND}" -C Release -V)
  endif()
endfunction()
function(exec_linker_genex test lang type)
  run_linker_genex(${ARGV} EXECUTE)
endfunction()

set(languages C CXX)
foreach(lang IN ITEMS OBJC Fortran CUDA HIP)
  if(CMake_TEST_${lang})
    list(APPEND languages ${lang})
    if(lang STREQUAL OBJC)
    list(APPEND languages OBJCXX)
    endif()
  endif()
endforeach()

foreach(lang IN LISTS languages)
  foreach(type IN ITEMS ID FRONTEND_VARIANT)
    run_linker_genex(NonValidTarget-COMPILER_LINKER ${lang} ${type} CHECK_RESULT)
    exec_linker_genex(COMPILER_LINKER ${lang} ${type})
  endforeach()
endforeach()

if(RunCMake_GENERATOR_IS_MULTI_CONFIG)
  set(RunCMake_TEST_OPTIONS [==[-DCMAKE_CONFIGURATION_TYPES=CustomConfig]==])
else()
  set(RunCMake_TEST_OPTIONS [==[-DCMAKE_BUILD_TYPE=CustomConfig]==])
endif()
run_cmake(CONFIG-multiple-entries)
if(RunCMake_GENERATOR_IS_MULTI_CONFIG)
  set(RunCMake_TEST_OPTIONS [==[-DCMAKE_CONFIGURATION_TYPES=]==])
else()
  set(RunCMake_TEST_OPTIONS [==[-DCMAKE_BUILD_TYPE=]==])
endif()
run_cmake(CONFIG-empty-entries)
unset(RunCMake_TEST_OPTIONS)

run_cmake(CMP0199-WARN)
run_cmake_build(CMP0199-OLD)
run_cmake_build(CMP0199-NEW)

set(RunCMake_TEST_OPTIONS -DCMAKE_POLICY_DEFAULT_CMP0085:STRING=OLD)
run_cmake(CMP0085-OLD)
unset(RunCMake_TEST_OPTIONS)

run_cmake(CMP0085-WARN)

set(RunCMake_TEST_OPTIONS -DCMAKE_POLICY_DEFAULT_CMP0085:STRING=NEW)
run_cmake(CMP0085-NEW)
unset(RunCMake_TEST_OPTIONS)
