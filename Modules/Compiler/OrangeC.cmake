include_guard()

macro(__compiler_orangec lang)
  if ("x${lang}" MATCHES "^x(C|CXX)$")
    set(CMAKE_${lang}_CREATE_PREPROCESSED_SOURCE "<CMAKE_${lang}_COMPILER> ${_ORANGEC_COMPILE_${lang}} -! <SOURCE> <DEFINES> <INCLUDES> <FLAGS> +i -o <PREPROCESSED_SOURCE>")
    set(CMAKE_${lang}_CREATE_ASSEMBLY_SOURCE     "<CMAKE_${lang}_COMPILER> ${_ORANGEC_COMPILE_${lang}} -! <SOURCE> <DEFINES> <INCLUDES> <FLAGS> -S -o <ASSEMBLY_SOURCE>")
  endif()
  set(CMAKE_${lang}_COMPILE_OBJECT             "<CMAKE_${lang}_COMPILER> ${_ORANGEC_COMPILE_${lang}} -! -c <SOURCE> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT>")
  unset(_ORANGEC_COMPILE_${lang})

  set(CMAKE_DEPFILE_FLAGS_${lang} "-MD -MT <DEP_TARGET> -MF <DEP_FILE>")
  set(CMAKE_${lang}_DEPFILE_FORMAT gcc)
  set(CMAKE_${lang}_DEPENDS_USE_COMPILER TRUE)

  set(CMAKE_${lang}_LINK_MODE DRIVER)

  string(APPEND CMAKE_${lang}_FLAGS_INIT " ")
  string(APPEND CMAKE_${lang}_FLAGS_DEBUG_INIT " -g")
  string(APPEND CMAKE_${lang}_FLAGS_RELEASE_INIT " -O2 -DNDEBUG")
  string(APPEND CMAKE_${lang}_FLAGS_MINSIZEREL_INIT " -O1 -DNDEBUG")
  string(APPEND CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT " -O2 -g -DNDEBUG")

  set(CMAKE_${lang}_CREATE_STATIC_LIBRARY
    "<CMAKE_${lang}_COMPILER> -! -static -o <TARGET> <LINK_FLAGS> <OBJECTS> ")
  set(CMAKE_${lang}_LINK_EXECUTABLE "<CMAKE_${lang}_COMPILER> -! <FLAGS> -o <TARGET> --out-implib <TARGET_IMPLIB> <LINK_FLAGS> <OBJECTS> <LINK_LIBRARIES>")
  set(CMAKE_${lang}_CREATE_SHARED_LIBRARY
    "<CMAKE_${lang}_COMPILER> -! <FLAGS> -o <TARGET> --out-implib <TARGET_IMPLIB> <CMAKE_SHARED_LIBRARY_${lang}_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> <OBJECTS> <LINK_LIBRARIES>")
  set(CMAKE_${lang}_CREATE_SHARED_MODULE "${CMAKE_${lang}_CREATE_SHARED_LIBRARY}")

  set(CMAKE_LIBRARY_PATH_FLAG "-L")
  set(CMAKE_SHARED_LIBRARY_CREATE_${lang}_FLAGS "-! -shared")

  set(CMAKE_${lang}_RESPONSE_FILE_FLAG "@")
  set(CMAKE_${lang}_RESPONSE_FILE_LINK_FLAG "@")
endmacro()
