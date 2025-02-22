# Add packaging directives for timemory
set(CPACK_PACKAGE_NAME ${PROJECT_NAME}-timem)
set(CPACK_PACKAGE_VENDOR
    "The Regents of the University of California, through Lawrence Berkeley National Laboratory"
    )
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Performance Analysis Toolkit and Suite of Tools for C/C++/CUDA/HIP/Fortran/Python")
set(CPACK_PACKAGE_VERSION_MAJOR "${timemory_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${timemory_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${timemory_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "jonathan.madsen@amd.com")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")

# Debian package specific variables
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/ROCm/timemory")
if(DEFINED ENV{CPACK_DEBIAN_PACKAGE_RELEASE})
    set(CPACK_DEBIAN_PACKAGE_RELEASE $ENV{CPACK_DEBIAN_PACKAGE_RELEASE})
elseif(NOT CPACK_DEBIAN_PACKAGE_RELEASE)
    set(CPACK_DEBIAN_PACKAGE_RELEASE "local")
endif()

# RPM package specific variables
if(DEFINED CPACK_PACKAGING_INSTALL_PREFIX)
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "${CPACK_PACKAGING_INSTALL_PREFIX}")
endif()

if(DEFINED ENV{CPACK_RPM_PACKAGE_RELEASE})
    set(CPACK_RPM_PACKAGE_RELEASE $ENV{CPACK_RPM_PACKAGE_RELEASE})
elseif(NOT CPACK_RPM_PACKAGE_RELEASE)
    set(CPACK_RPM_PACKAGE_RELEASE "local")
endif()

# Get rpm distro
if(CPACK_RPM_PACKAGE_RELEASE)
    set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
endif()

# Prepare final version for the CPACK use
set(CPACK_PACKAGE_VERSION
    "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}"
    )

# Set the names now using CPACK utility
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # auto-generate deps based on shared libs
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON) # generate list of shared libs provided by
                                             # package
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS_POLICY ">=")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS
    ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
    ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME})

set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_PACKAGE_AUTOREQ ON) # auto-generate deps based on shared libs
set(CPACK_RPM_PACKAGE_AUTOPROV ON) # generate list of shared libs provided by package

include(CPack)
