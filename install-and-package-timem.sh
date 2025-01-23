#!/bin/bash -e

: ${SOURCE_DIR:="$(dirname $(realpath ${BASH_SOURCE[0]}))"}
: ${BINARY_DIR:="${SOURCE_DIR}/build-timemory-timem"}
: ${INSTALL_DIR:="${SOURCE_DIR}/install-timemory-timem"}
: ${NJOBS:=2}
: ${GENERATORS:="STGZ"}

cmake                                                                                   \
    -B ${BINARY_DIR}                                                                    \
    ${SOURCE_DIR}                                                                       \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}                                               \
    -DCMAKE_BUILD_TYPE=Release                                                          \
    -DCPACK_{DEBIAN,RPM}_PACKAGE_RELEASE=Linux                                          \
    -DTIMEMORY_SKIP_BUILD=ON                                                            \
    -DTIMEMORY_BUILD_{TOOLS,ERT,STUBS,PID,LIBUNWIND,TESTING,EXAMPLES,BUILD_FORTRAN}=OFF \
    -DTIMEMORY_{BUILD,USE}_YAML=OFF                                                     \
    -DTIMEMORY_INSTALL_{ALL,HEADERS,CONFIG,PAPI,LIBRARIES}=OFF                          \
    -DTIMEMORY_BUILD_{TIMEM,PAPI,PORTABLE,TOOLS_PAPI}=ON                                \
    "${@}"

pushd ${BINARY_DIR}
cmake --build . --target all     --parallel ${NJOBS} -- VERBOSE=0
cmake --build . --target install --parallel ${NJOBS}
cpack -G "${GENERATORS}" -D CPACK_PACKAGING_INSTALL_PREFIX=/usr/local -D CPACK_PACKAGE_DIRECTORY=${PWD}/packaging
rm -rf ${PWD}/packaging/_CPack_Packages
popd
