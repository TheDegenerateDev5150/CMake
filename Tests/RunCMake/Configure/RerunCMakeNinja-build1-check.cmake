file(READ ${stamp} content)
if(NOT content STREQUAL 1)
  set(RunCMake_TEST_FAILED "Expected stamp '1' but got: '${content}'")
endif()
