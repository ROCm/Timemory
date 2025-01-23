# ======================================================================================
# PAPI.cmake
#
# Configure papi for rocprofiler-systems
#
# ======================================================================================

include_guard(GLOBAL)

timemory_checkout_git_submodule(
    RELATIVE_PATH external/papi
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    REPO_URL https://github.com/icl-utk-edu/papi.git
    REPO_BRANCH 3ce9001dff49e1b6b1653ffb429808795f71a0bd
    TEST_FILE src/configure)

set(PAPI_LIBPFM_SOVERSION
    "4.13.0"
    CACHE STRING "libpfm.so version")

set(TIMEMORY_PAPI_SOURCE_DIR ${PROJECT_BINARY_DIR}/external/papi/source)
set(TIMEMORY_PAPI_INSTALL_DIR ${PROJECT_BINARY_DIR}/external/papi/install)

if(NOT EXISTS "${TIMEMORY_PAPI_SOURCE_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/external/papi
                ${TIMEMORY_PAPI_SOURCE_DIR})
endif()

if(NOT EXISTS "${TIMEMORY_PAPI_INSTALL_DIR}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory
                            ${TIMEMORY_PAPI_INSTALL_DIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory
                            ${TIMEMORY_PAPI_INSTALL_DIR}/include)
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory
                            ${TIMEMORY_PAPI_INSTALL_DIR}/lib)
    execute_process(
        COMMAND
            ${CMAKE_COMMAND} -E touch ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpapi.a
            ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpfm.a
            ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpfm.so)
    set(_TIMEMORY_PAPI_BUILD_BYPRODUCTS
        ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpapi.a
        ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpfm.a
        ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpfm.so)
endif()

# Set TIMEMORY_PAPI_CONFIGURE_JOBS for commands that need to be run nonparallel
set(TIMEMORY_PAPI_CONFIGURE_JOBS 1)

timemory_add_option(TIMEMORY_PAPI_AUTO_COMPONENTS "Automatically enable components" OFF)

# -------------- PACKAGES -----------------------------------------------------

set(_TIMEMORY_VALID_PAPI_COMPONENTS
    appio
    bgpm
    coretemp
    coretemp_freebsd
    cuda
    emon
    example
    host_micpower
    infiniband
    intel_gpu
    io
    libmsr
    lmsensors
    lustre
    micpower
    mx
    net
    nvml
    pcp
    perfctr
    perfctr_ppc
    perf_event
    perf_event_uncore
    perfmon2
    perfmon_ia64
    perfnec
    powercap
    powercap_ppc
    rapl
    rocm
    rocm_smi
    sde
    sensors_ppc
    stealtime
    sysdetect
    vmware)
set(TIMEMORY_VALID_PAPI_COMPONENTS
    "${_TIMEMORY_VALID_PAPI_COMPONENTS}"
    CACHE STRING "Valid PAPI components")
mark_as_advanced(TIMEMORY_VALID_PAPI_COMPONENTS)

# default components which do not require 3rd-party headers or libraries
set(_TIMEMORY_PAPI_COMPONENTS
    appio
    coretemp
    io
    infiniband
    # lustre micpower mx
    net
    perf_event
    perf_event_uncore
    # rapl stealtime
    )

if(TIMEMORY_PAPI_AUTO_COMPONENTS)
    # lmsensors
    find_path(TIMEMORY_PAPI_LMSENSORS_ROOT_DIR NAMES include/sensors/sensors.h
                                                     include/sensors.h)

    if(TIMEMORY_PAPI_LMSENSORS_ROOT_DIR)
        list(APPEND _TIMEMORY_PAPI_COMPONENTS lmsensors)
    endif()

    # pcp
    find_path(TIMEMORY_PAPI_PCP_ROOT_DIR NAMES include/pcp/impl.h)
    find_library(
        TIMEMORY_PAPI_PCP_LIBRARY
        NAMES pcp
        PATH_SUFFIXES lib lib64)

    if(TIMEMORY_PAPI_PCP_ROOT_DIR AND TIMEMORY_PAPI_PCP_LIBRARY)
        list(APPEND _TIMEMORY_PAPI_COMPONENTS pcp)
    endif()
endif()

# set the TIMEMORY_PAPI_COMPONENTS cache variable
set(TIMEMORY_PAPI_COMPONENTS
    "${_TIMEMORY_PAPI_COMPONENTS}"
    CACHE STRING "PAPI components")
timemory_add_feature(TIMEMORY_PAPI_COMPONENTS "PAPI components")
string(REPLACE ";" "\ " _TIMEMORY_PAPI_COMPONENTS "${TIMEMORY_PAPI_COMPONENTS}")
set(TIMEMORY_PAPI_EXTRA_ENV)

foreach(_COMP ${TIMEMORY_PAPI_COMPONENTS})
    string(REPLACE ";" ", " _TIMEMORY_VALID_PAPI_COMPONENTS_MSG
                   "${TIMEMORY_VALID_PAPI_COMPONENTS}")
    if(NOT "${_COMP}" IN_LIST TIMEMORY_VALID_PAPI_COMPONENTS)
        timemory_message(
            AUTHOR_WARNING
            "TIMEMORY_PAPI_COMPONENTS contains an unknown component '${_COMP}'. Known components: ${_TIMEMORY_VALID_PAPI_COMPONENTS_MSG}"
            )
    endif()
    unset(_TIMEMORY_VALID_PAPI_COMPONENTS_MSG)
endforeach()

if("rocm" IN_LIST TIMEMORY_PAPI_COMPONENTS)
    find_package(ROCmVersion REQUIRED)
    list(APPEND TIMEMORY_PAPI_EXTRA_ENV PAPI_ROCM_ROOT=${ROCmVersion_DIR})
endif()

if("lmsensors" IN_LIST TIMEMORY_PAPI_COMPONENTS AND TIMEMORY_PAPI_LMSENSORS_ROOT_DIR)
    list(APPEND TIMEMORY_PAPI_EXTRA_ENV
         PAPI_LMSENSORS_ROOT=${TIMEMORY_PAPI_LMSENSORS_ROOT_DIR})
endif()

if("pcp" IN_LIST TIMEMORY_PAPI_COMPONENTS AND TIMEMORY_PAPI_PCP_ROOT_DIR)
    list(APPEND TIMEMORY_PAPI_EXTRA_ENV PAPI_PCP_ROOT=${TIMEMORY_PAPI_PCP_ROOT_DIR})
endif()

if("perf_event_uncore" IN_LIST TIMEMORY_PAPI_COMPONENTS AND NOT "perf_event" IN_LIST
                                                            TIMEMORY_PAPI_COMPONENTS)
    timemory_message(
        FATAL_ERROR
        "TIMEMORY_PAPI_COMPONENTS :: 'perf_event_uncore' requires 'perf_event' component")
endif()

find_program(
    MAKE_EXECUTABLE
    NAMES make gmake
    PATH_SUFFIXES bin)

if(NOT MAKE_EXECUTABLE)
    timemory_message(
        FATAL_ERROR
        "make/gmake executable not found. Please re-run with -DMAKE_EXECUTABLE=/path/to/make"
        )
endif()

set(_PAPI_C_COMPILER ${CMAKE_C_COMPILER})
if(CMAKE_C_COMPILER_IS_CLANG)
    find_program(TIMEMORY_GNU_C_COMPILER NAMES gcc)
    if(TIMEMORY_GNU_C_COMPILER)
        set(_PAPI_C_COMPILER ${TIMEMORY_GNU_C_COMPILER})
    endif()
endif()
set(PAPI_C_COMPILER
    ${_PAPI_C_COMPILER}
    CACHE FILEPATH "C compiler used to compile PAPI")

include(ExternalProject)
externalproject_add(
    timemory-papi-build
    PREFIX ${PROJECT_BINARY_DIR}/external/papi
    SOURCE_DIR ${TIMEMORY_PAPI_SOURCE_DIR}/src
    BUILD_IN_SOURCE 1
    PATCH_COMMAND
        ${CMAKE_COMMAND} -E env CC=${PAPI_C_COMPILER}
        CFLAGS=-fPIC\ -O3\ -Wno-stringop-truncation\ -Wno-use-after-free LIBS=-lrt
        LDFLAGS=-lrt ${TIMEMORY_PAPI_EXTRA_ENV} <SOURCE_DIR>/configure --quiet
        --prefix=${TIMEMORY_PAPI_INSTALL_DIR} --with-static-lib=yes --with-shared-lib=no
        --with-perf-events --with-tests=no --with-components=${_TIMEMORY_PAPI_COMPONENTS}
        --libdir=${TIMEMORY_PAPI_INSTALL_DIR}/lib
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env
        CFLAGS=-fPIC\ -O3\ -Wno-stringop-truncation\ -Wno-use-after-free
        ${TIMEMORY_PAPI_EXTRA_ENV} ${MAKE_EXECUTABLE} static install -s -j
        ${TIMEMORY_PAPI_CONFIGURE_JOBS}
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E env
        CFLAGS=-fPIC\ -O3\ -Wno-stringop-truncation\ -Wno-use-after-free
        ${TIMEMORY_PAPI_EXTRA_ENV} ${MAKE_EXECUTABLE} utils install-utils -s
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${_TIMEMORY_PAPI_BUILD_BYPRODUCTS}")

# target for re-executing the installation
add_custom_target(
    timemory-papi-install
    COMMAND
        ${CMAKE_COMMAND} -E env
        CFLAGS=-fPIC\ -O3\ -Wno-stringop-truncation\ -Wno-use-after-free
        ${TIMEMORY_PAPI_EXTRA_ENV} ${MAKE_EXECUTABLE} static install -s
    COMMAND
        ${CMAKE_COMMAND} -E env
        CFLAGS=-fPIC\ -O3\ -Wno-stringop-truncation\ -Wno-use-after-free
        ${TIMEMORY_PAPI_EXTRA_ENV} ${MAKE_EXECUTABLE} utils install-utils -s
    WORKING_DIRECTORY ${TIMEMORY_PAPI_SOURCE_DIR}/src
    COMMENT "Installing PAPI...")

add_dependencies(timemory-papi-install timemory-papi-build)

add_custom_target(
    timemory-papi-clean
    COMMAND ${MAKE_EXECUTABLE} distclean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${TIMEMORY_PAPI_INSTALL_DIR}/include/*
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${TIMEMORY_PAPI_INSTALL_DIR}/lib/*
    COMMAND
        ${CMAKE_COMMAND} -E touch ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpapi.a
        ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpfm.a
        ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpfm.so
    WORKING_DIRECTORY ${TIMEMORY_PAPI_SOURCE_DIR}/src
    COMMENT "Cleaning PAPI...")

set(PAPI_ROOT_DIR
    ${TIMEMORY_PAPI_INSTALL_DIR}
    CACHE PATH "Root PAPI installation" FORCE)
set(PAPI_INCLUDE_DIR
    ${TIMEMORY_PAPI_INSTALL_DIR}/include
    CACHE PATH "PAPI include folder" FORCE)
set(PAPI_LIBRARY
    ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpapi.a
    CACHE FILEPATH "PAPI library" FORCE)
set(PAPI_pfm_LIBRARY
    ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpfm.so
    CACHE FILEPATH "PAPI library" FORCE)
set(PAPI_STATIC_LIBRARY
    ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpapi.a
    CACHE FILEPATH "PAPI library" FORCE)
set(PAPI_pfm_STATIC_LIBRARY
    ${TIMEMORY_PAPI_INSTALL_DIR}/lib/libpfm.a
    CACHE FILEPATH "PAPI library" FORCE)

target_include_directories(timemory-papi SYSTEM
                           INTERFACE $<BUILD_INTERFACE:${PAPI_INCLUDE_DIR}>)
target_link_libraries(timemory-papi INTERFACE $<BUILD_INTERFACE:${PAPI_LIBRARY}>
                                              $<BUILD_INTERFACE:${PAPI_pfm_LIBRARY}>)
timemory_target_compile_definitions(timemory-papi INTERFACE TIMEMORY_USE_PAPI
                                    $<BUILD_INTERFACE:TIMEMORY_USE_PAPI=1>)

add_dependencies(timemory-papi timemory-papi-install)

set(BINARY_NAME_PREFIX "${PROJECT_NAME}")
if(TIMEMORY_INSTALL_PAPI)
    install(
        DIRECTORY ${TIMEMORY_PAPI_INSTALL_DIR}/lib/
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}
        COMPONENT papi
        FILES_MATCHING
        PATTERN "*.so*")

    foreach(
        _UTIL_EXE
        papi_avail
        papi_clockres
        papi_command_line
        papi_component_avail
        papi_cost
        papi_decode
        papi_error_codes
        papi_event_chooser
        papi_hardware_avail
        papi_hl_output_writer.py
        papi_mem_info
        papi_multiplex_cost
        papi_native_avail
        papi_version
        papi_xml_event_info)

        string(REPLACE "_" "-" _UTIL_EXE_INSTALL_NAME
                       "${BINARY_NAME_PREFIX}-${_UTIL_EXE}")

        # RPM installer on RedHat/RockyLinux throws error that #!/usr/bin/python should
        # either be #!/usr/bin/python2 or #!/usr/bin/python3
        if(_UTIL_EXE STREQUAL "papi_hl_output_writer.py")
            file(
                READ
                "${PROJECT_BINARY_DIR}/external/papi/source/src/high-level/scripts/${_UTIL_EXE}"
                _HL_OUTPUT_WRITER)
            string(REPLACE "#!/usr/bin/python\n" "#!/usr/bin/python3\n" _HL_OUTPUT_WRITER
                           "${_HL_OUTPUT_WRITER}")
            file(MAKE_DIRECTORY "${TIMEMORY_PAPI_INSTALL_DIR}/bin")
            file(WRITE "${TIMEMORY_PAPI_INSTALL_DIR}/bin/${_UTIL_EXE}3"
                 "${_HL_OUTPUT_WRITER}")
            set(_UTIL_EXE "${_UTIL_EXE}3")
        endif()

        install(
            PROGRAMS ${TIMEMORY_PAPI_INSTALL_DIR}/bin/${_UTIL_EXE}
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            RENAME ${_UTIL_EXE_INSTALL_NAME}
            COMPONENT papi
            OPTIONAL)
    endforeach()
else()
    install(
        DIRECTORY ${TIMEMORY_PAPI_INSTALL_DIR}/lib/
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}
        COMPONENT papi
        FILES_MATCHING
        PATTERN "*pfm.so*")
    foreach(_UTIL_EXE papi_avail papi_component_avail papi_event_chooser
                      papi_hardware_avail papi_native_avail)

        string(REPLACE "_" "-" _UTIL_EXE_INSTALL_NAME
                       "${BINARY_NAME_PREFIX}-${_UTIL_EXE}")

        # RPM installer on RedHat/RockyLinux throws error that #!/usr/bin/python should
        # either be #!/usr/bin/python2 or #!/usr/bin/python3
        if(_UTIL_EXE STREQUAL "papi_hl_output_writer.py")
            file(
                READ
                "${PROJECT_BINARY_DIR}/external/papi/source/src/high-level/scripts/${_UTIL_EXE}"
                _HL_OUTPUT_WRITER)
            string(REPLACE "#!/usr/bin/python\n" "#!/usr/bin/python3\n" _HL_OUTPUT_WRITER
                           "${_HL_OUTPUT_WRITER}")
            file(MAKE_DIRECTORY "${TIMEMORY_PAPI_INSTALL_DIR}/bin")
            file(WRITE "${TIMEMORY_PAPI_INSTALL_DIR}/bin/${_UTIL_EXE}3"
                 "${_HL_OUTPUT_WRITER}")
            set(_UTIL_EXE "${_UTIL_EXE}3")
        endif()

        install(
            PROGRAMS ${TIMEMORY_PAPI_INSTALL_DIR}/bin/${_UTIL_EXE}
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            RENAME ${_UTIL_EXE_INSTALL_NAME}
            COMPONENT papi
            OPTIONAL)
    endforeach()
endif()
