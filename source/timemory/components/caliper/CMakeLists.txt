if(TIMEMORY_USE_CALIPER)
    set(NAME caliper)
    set(DEPS timemory::timemory-caliper)

    file(GLOB_RECURSE header_files ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
    file(GLOB_RECURSE source_files ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

    timemory_build_intermediate_library(
        USE_INTERFACE
        NAME ${NAME}
        TARGET ${NAME}-component
        CATEGORY COMPONENT
        FOLDER ${CMAKE_INSTALL_LIBDIR}
        HEADERS ${header_files}
        SOURCES ${source_files}
        DEPENDS ${DEPS}
        PROPERTY_DEPENDS GLOBAL)
endif()
