cmake_minimum_required(VERSION 3.16)
project(hallmodel_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# -ffp-contract=off is part of the parity contract. Fused multiply-add (FMA)
# fuses `a*b + c` into a single rounding, which is compiler- and architecture-
# dependent. Disabling it makes outputs reproducible across same-flag builds —
# the same flag the upstream R package uses (see src/Makevars in INSP-RH/bw).
add_compile_options(-ffp-contract=off)

add_library(bw_core STATIC
    src/adult.cpp
    src/child.cpp
    src/energy.cpp
)
add_library(hallmodel::core ALIAS bw_core)

target_include_directories(bw_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(bw_core PUBLIC cxx_std_17)

# Install: downstream consumers can `find_package(hallmodel CONFIG REQUIRED)`.
include(GNUInstallDirs)
install(TARGETS bw_core
    EXPORT hallmodelTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/bw DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT hallmodelTargets
    FILE hallmodelTargets.cmake
    NAMESPACE hallmodel::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hallmodel
)

option(HALLMODEL_BUILD_PARITY_RUNNER "Build the parity_runner used by proof/verify.sh" ON)
if(HALLMODEL_BUILD_PARITY_RUNNER)
    add_subdirectory(tests)
endif()
