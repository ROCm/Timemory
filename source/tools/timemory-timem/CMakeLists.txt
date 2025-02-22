# no windows support
if(WIN32)
    return()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET "hidden")
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# ----------------------------------------------------------------------------------------#
# Build and install timem tool
#
if(NOT TIMEMORY_BUILD_TIMEM OR TIMEMORY_BUILD_EXCLUDE_FROM_ALL)
    # if not enabled as option, make it available to be built and installed
    set(_EXCLUDE EXCLUDE_FROM_ALL)
endif()

add_library(timemory-timem-libexplain INTERFACE)
add_library(timemory::timem-libexplain ALIAS timemory-timem-libexplain)

timemory_add_option(TIMEMORY_BUILD_TOOLS_LIBEXPLAIN
                    "Enable using libexplain to diagnose errors" OFF NO_FEATURE)
timemory_add_option(
    TIMEMORY_BUILD_TOOLS_PAPI "Enable papi when building tools such as timem"
    ${TIMEMORY_USE_PAPI} NO_FEATURE)
timemory_add_option(TIMEMORY_BUILD_TOOLS_PAPI_STATIC "Use static linking to papi" OFF
                    NO_FEATURE)

if(TIMEMORY_BUILD_TOOLS_LIBEXPLAIN)
    find_path(
        LIBEXPLAIN_INCLUDE_DIR
        NAMES libexplain/execvp.h
        PATH_SUFFIXES include REQUIRED)

    find_library(
        LIBEXPLAIN_LIBRARY
        NAMES explain
        PATH_SUFFIXES lib lib64 lib32 REQUIRED)

    timemory_target_compile_definitions(timemory-timem-libexplain INTERFACE
                                        TIMEMORY_USE_LIBEXPLAIN)
    target_include_directories(timemory-timem-libexplain
                               INTERFACE ${LIBEXPLAIN_INCLUDE_DIR})
    target_link_libraries(timemory-timem-libexplain INTERFACE ${LIBEXPLAIN_LIBRARY})
endif()

# tests whether the static papi library can be linked (errors typically occur when
# libpapi.a is not PIC and PIE gets enabled)
function(TEST_PAPI_STATIC_LINKAGE _VAR)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(EXE_INCLUDE "#include <papi.h>")
    set(EXE_BODY "PAPI_thread_init(0); return (PAPI_is_initialized() == 0) ? 0 : 1;")
    configure_file(${PROJECT_SOURCE_DIR}/cmake/Templates/exe.cpp.in
                   ${PROJECT_BINARY_DIR}/compile-tests/papi_link.cpp @ONLY)
    try_compile(
        _RET ${PROJECT_BINARY_DIR}/compile-tests
        ${PROJECT_BINARY_DIR}/compile-tests/papi_link.cpp
        LINK_LIBRARIES ${ARGN}
        OUTPUT_VARIABLE _OUT)
    set(${_VAR}
        ${_RET}
        PARENT_SCOPE)
endfunction()

set(_RPATH ON)
add_library(timemory-timem-papi INTERFACE)
add_library(timemory::timem-papi ALIAS timemory-timem-papi)

if(NOT DEFINED TIMEM_PAPI_TARGET OR NOT TIMEM_PAPI_TARGET)
    if(TIMEMORY_BUILD_TOOLS_PAPI)
        find_package(PAPI REQUIRED)
    endif()
    if(TIMEMORY_BUILD_TOOLS_PAPI_STATIC AND (PAPI_STATIC_LIBRARIES OR TARGET
                                                                      PAPI::papi-static))
        test_papi_static_linkage(_USE_PAPI_STATIC PAPI::papi-static)
        if(_USE_PAPI_STATIC)
            target_link_libraries(timemory-timem-papi INTERFACE PAPI::papi-static)
            set(_RPATH OFF)
            timemory_target_compile_definitions(timemory-timem-papi INTERFACE
                                                TIMEMORY_USE_PAPI)
        else()
            target_link_libraries(timemory-timem-papi INTERFACE timemory::timemory-papi)
        endif()
    elseif(TARGET PAPI::papi-shared)
        target_link_libraries(timemory-timem-papi INTERFACE timemory::timemory-papi
                                                            PAPI::papi-shared)
        timemory_target_compile_definitions(timemory-timem-papi INTERFACE
                                            TIMEMORY_USE_PAPI)
    else()
        target_link_libraries(timemory-timem-papi INTERFACE timemory::timemory-papi)
    endif()
elseif(TIMEM_PAPI_TARGET)
    target_link_libraries(timemory-timem-papi INTERFACE ${TIMEM_PAPI_TARGET})
endif()

#
# Compile options
#
add_library(timemory-timem-compile-options INTERFACE)
add_library(timemory::timemory-timem-compile-options ALIAS timemory-timem-compile-options)
add_target_flag_if_avail(timemory-timem-compile-options "-fstack-protector-strong"
                         "-Wstack-protector")
target_compile_options(
    timemory-timem-compile-options
    INTERFACE $<$<COMPILE_LANGUAGE:C>:$<$<C_COMPILER_ID:GNU>:-static-libgcc>>
              $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU>:-static-libgcc>>)
target_link_options(
    timemory-timem-compile-options INTERFACE
    $<$<COMPILE_LANGUAGE:C>:$<$<C_COMPILER_ID:GNU,Clang>:-static-libgcc>>
    $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU,Clang>:-static-libgcc>>)

#
# timem md5sum
#
add_library(timem-md5sum EXCLUDE_FROM_ALL OBJECT md5.cpp md5.hpp)

target_link_libraries(
    timem-md5sum
    PRIVATE timemory::timemory-compile-options timemory::timemory-develop-options
            timemory::timemory-headers timemory::timemory-timem-compile-options)

#
# timem library
#
add_library(timem-library ${_EXCLUDE} SHARED)
add_library(timemory::timem-library ALIAS timem-library)
add_dependencies(timem-library timem-md5sum)

target_sources(timem-library PRIVATE libtimem.cpp libtimem.cpp main.c
                                     $<TARGET_OBJECTS:timem-md5sum>)

target_link_libraries(
    timem-library
    PRIVATE timemory::timemory-compile-options timemory::timemory-develop-options
            timemory::timemory-threading timemory::timemory-headers timemory::timem-papi
            timemory::timem-libexplain timemory::timemory-timem-compile-options)

set_target_properties(
    timem-library
    PROPERTIES OUTPUT_NAME timem
               POSITION_INDEPENDENT_CODE ON
               RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
               LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
               ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
               INSTALL_RPATH_USE_LINK_PATH ${_RPATH}
               SOVERSION ${PROJECT_VERSION_MAJOR}
               VERSION ${PROJECT_VERSION}
               BUILD_RPATH "\$ORIGIN:\$ORIGIN/${PROJECT_NAME}"
               INSTALL_RPATH "\$ORIGIN:\$ORIGIN/${PROJECT_NAME}")

install(
    TARGETS timem-library
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT tools
    OPTIONAL)

#
# timem executable
#
add_executable(timem ${_EXCLUDE})
add_executable(timemory::timem ALIAS timem)
add_dependencies(timem timem-library)

target_sources(timem PRIVATE timem.cpp)

if(TIMEMORY_BUILD_TOOLS_PAPI)
    target_compile_definitions(timem PRIVATE TIMEMORY_USE_PAPI=1)
endif()

target_link_libraries(
    timem
    PRIVATE timemory::timemory-compile-options timemory::timemory-develop-options
            timemory::timemory-threading timemory::timemory-headers
            timemory::timemory-timem-compile-options)

if(CMAKE_CXX_COMPILER_IS_GNU AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
    target_link_libraries(timem PRIVATE stdc++fs)
elseif(CMAKE_CXX_COMPILER_IS_CLANG AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(timem PRIVATE c++fs)
endif()

set_target_properties(
    timem
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
               LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
               ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
               INSTALL_RPATH_USE_LINK_PATH ${_RPATH}
               BUILD_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
               INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

# timem installation
install(
    TARGETS timem
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT tools
    OPTIONAL)
