cmake_minimum_required(VERSION 3.10)

set(BOREALIS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/borealis")
set(BOREALIS_LIBRARY "${BOREALIS_ROOT}/library")

include("${BOREALIS_LIBRARY}/cmake/commonOption.cmake")
include("${BOREALIS_LIBRARY}/cmake/toolchain.cmake")

project(playwise_borealis_poc LANGUAGES C CXX)

if (NOT PLATFORM_SWITCH)
    message(FATAL_ERROR "The PlayWise Borealis PoC is a Nintendo Switch-only target")
endif ()

if (NOT USE_DEKO3D)
    message(FATAL_ERROR "The pinned PoC backend is deko3d; configure with -DUSE_DEKO3D=ON")
endif ()

if (NOT DEFINED PLAYWISE_VERSION)
    set(PLAYWISE_VERSION "0.0.0")
endif ()

set(POC_TARGET playwise-borealis-poc)
set(POC_TITLE "PlayWise Borealis PoC")
set(POC_AUTHOR "PlayWise internal")
set(POC_DISPLAY_VERSION "${PLAYWISE_VERSION}-poc")
set(POC_ROMFS "${CMAKE_CURRENT_BINARY_DIR}/romfs")
set(POC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../../companion/nro/icon.png")

add_subdirectory("${BOREALIS_LIBRARY}" "${CMAKE_CURRENT_BINARY_DIR}/borealis")

add_executable(${POC_TARGET}
    source/main.cpp
    "${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c"
)
set_target_properties(${POC_TARGET} PROPERTIES CXX_STANDARD 17)
target_compile_options(${POC_TARGET} PRIVATE -ffunction-sections -fdata-sections)
target_link_options(${POC_TARGET} PRIVATE ${APP_PLATFORM_LINK_OPTION})
target_link_libraries(${POC_TARGET} PRIVATE borealis ${APP_PLATFORM_LIB})

file(MAKE_DIRECTORY "${POC_ROMFS}/material")
file(COPY "${BOREALIS_ROOT}/resources/material/MaterialIcons-Regular.ttf"
    DESTINATION "${POC_ROMFS}/material")
file(COPY "${BOREALIS_ROOT}/resources/i18n" DESTINATION "${POC_ROMFS}")
gen_dksh("${POC_ROMFS}/shaders")

add_custom_target(${POC_TARGET}.nro DEPENDS ${POC_TARGET}
    COMMAND ${NX_NACPTOOL_EXE} --create "${POC_TITLE}" "${POC_AUTHOR}"
        "${POC_DISPLAY_VERSION}" ${POC_TARGET}.nacp
    COMMAND ${NX_ELF2NRO_EXE} ${POC_TARGET}.elf ${POC_TARGET}.nro
        --icon=${POC_ICON} --nacp=${POC_TARGET}.nacp --romfsdir=${POC_ROMFS}
    VERBATIM
)
