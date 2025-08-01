include(RunCMake)

if (DEFINED with_qt_version)
  set(RunCMake_TEST_OPTIONS
    -Dwith_qt_version=${with_qt_version}
    "-DQt${with_qt_version}_DIR:PATH=${Qt${with_qt_version}_DIR}"
    "-DCMAKE_PREFIX_PATH:STRING=${CMAKE_PREFIX_PATH}"
  )
  run_cmake(AutoMocIncludeDirectories)
  if (CMAKE_GENERATOR MATCHES "(Ninja|Makefiles|Visual Studio)")
    run_cmake(AutoMocIncludeDirectoriesShort)
  endif ()
endif()
