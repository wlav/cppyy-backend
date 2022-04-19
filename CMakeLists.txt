project(cppyy-backend)
cmake_minimum_required(VERSION 3.10)
include(ExternalProject)

set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)

add_library(cppyy-backend SHARED
    clingwrapper/src/clingwrapper.cxx
)

# set_target_properties(cppyy-backend PROPERTIES VERSION ${PROJECT_VERSION})

# set_target_properties(cppyy-backend PROPERTIES PUBLIC_HEADER include/cpp_cppyy.h)

target_include_directories(cppyy-backend PRIVATE clingwrapper/src)


add_library(libInterOp IMPORTED STATIC GLOBAL)

set(_interop_static_archive_name ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}${CMAKE_STATIC_LIBRARY_PREFIX}clangInterOp${CMAKE_STATIC_LIBRARY_SUFFIX})
if("${CMAKE_GENERATOR}" STREQUAL "Ninja")
    set(_interop_byproducts ${_interop_static_archive_name})
endif()

if(DEFINED InterOp_DIR)
    set(_interop_install_dir ${InterOp_DIR})
else()
    set(_interop_install_dir ${CMAKE_BINARY_DIR}/InterOp/install)

    if(DEFINED USE_CLING)
        list(APPEND _interop_cmake_args "-DUSE_CLING=${USE_CLING}")
    endif()

    if(DEFINED Cling_DIR)
        list(APPEND _interop_cmake_args "-DCling_DIR=${Cling_DIR}")
    endif()

    if(DEFINED LLVM_DIR)
        list(APPEND _interop_cmake_args "-DLLVM_DIR=${LLVM_DIR}")
    endif()

    if(DEFINED Clang_DIR)
        list(APPEND _interop_cmake_args "-DClang_DIR=${Clang_DIR}")
    endif()

    list(APPEND _interop_cmake_args "-DCMAKE_INSTALL_PREFIX=${_interop_install_dir}")

    set(_interop_build_type ${CMAKE_CFG_INTDIR})
    set(_interop_cmake_logging_settings
          LOG_DOWNLOAD ON
          LOG_CONFIGURE ON
          LOG_BUILD ON
          LOG_INSTALL ON
          LOG_OUTPUT_ON_FAILURE ON
        )

    ExternalProject_Add(InterOp 
        GIT_REPOSITORY https://github.com/compiler-research/InterOp.git
        GIT_TAG main
        PREFIX "InterOp"
        CMAKE_ARGS ${_interop_cmake_args}
        BUILD_BYPRODUCTS ${_interop_byproducts}
        ${_interop_cmake_logging_settings}
    )

    add_dependencies(libInterOp InterOp)
endif()

set_source_files_properties(clingwrapper/src/clingwrapper.cxx
  PROPERTIES COMPILE_DEFINITIONS "INTEROP_DIR=\"${_interop_install_dir}\"")

set_property(TARGET libInterOp PROPERTY IMPORTED_LOCATION ${_interop_install_dir}/lib/${_interop_static_archive_name})

if (APPLE)
set(_interop_link_flags -Wl,-force_load $<TARGET_FILE:libInterOp> -Wl)
elseif(MSVC)
set(_interop_link_flags "-WHOLEARCHIVE:" $<TARGET_FILE:libInterOp>)
else()
set(_interop_link_flags -Wl,--whole-archive $<TARGET_FILE:libInterOp> -Wl,--no-whole-archive)
endif()
target_link_libraries(cppyy-backend PUBLIC ${_interop_link_flags})

add_dependencies(cppyy-backend libInterOp)

# set(CLING_LIBS
#     ${PROJECT_SOURCE_DIR}/../cling/builddir/lib/libclingInterpreter.a
#     # clangAST
#     # clangBasic
#     # clangLex
# )

target_include_directories(cppyy-backend PUBLIC ${_interop_install_dir}/include)

# target_link_libraries(cppyy-backend ${CLING_LIBS})
# target_link_libraries(cppyy-backend ${PROJECT_SOURCE_DIR}/../cling/builddir/lib/libcling.so)

# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#     message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()
