cmake_minimum_required(VERSION 3.5)
set(XBE_TITLE xemu-dashboard)
set(XBOX_ISO_DIR ${CMAKE_CURRENT_BINARY_DIR}/xiso)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(NXDK_DEBUG_FLAG "y" CACHE STRING "Enable debug build for the NXDK submodule. 'y' or 'n'.")
endif()

project(xemu-dashboard C CXX ASM)

include(FetchContent)
include(FindPkgConfig)
include(PostbuildAction)
include(PrebuildNXDK)

set(CMAKE_ASM_FLAGS_DEBUG "${CMAKE_ASM_FLAGS_DEBUG} -g -gdwarf-4 -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -gdwarf-4 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -gdwarf-4 -Wall -Wextra")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -debug")

set(CMAKE_ASM_FLAGS_RELEASE "${CMAKE_ASM_FLAGS_RELEASE} -O2")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")

add_compile_options($<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:C>>:-gdwarf-4>)

add_definitions(-DXBOX -DNXDK -DSDL_DISABLE_JOYSTICK_INIT_DELAY -DFTP_CUSTOM_ROOT_PATH -DMEMP_NUM_NETBUF=6 -DMEMP_NUM_NETCONN=6)

add_executable(xemu-dashboard
    main.c
    menu_main.c
    menu_system_info.c
    menu_eeprom.c
    menu_install_dash.c
    support_dvd.c
    support_network.c
    support_text.c
    support_renderer.c
    support_updater.c lib/mbedtls/glue.c
    lib/ftpd/ftp_file.c lib/ftpd/ftp_server.c lib/ftpd/ftp.c
)

target_include_directories(xemu-dashboard PRIVATE lib)

# Bring in the DVD drive automount support
target_link_libraries(xemu-dashboard PUBLIC ${NXDK_DIR}/lib/libnxdk_automount_d.lib)
target_link_options(xemu-dashboard PRIVATE "-include:_automount_d_drive")

# Bring in networking support
target_link_libraries(xemu-dashboard PUBLIC ${NXDK_DIR}/lib/libnxdk_net.lib)
target_include_directories(xemu-dashboard PRIVATE ${NXDK_DIR}/lib/net/lwip/src/include ${NXDK_DIR}/lib/net/nforceif/include ${NXDK_DIR}/lib/net/nvnetdrv)

# Bring in pbkit support
target_link_libraries(xemu-dashboard PUBLIC ${NXDK_DIR}/lib/libpbkit.lib)

# Bring in SDL2 support
pkg_search_module(SDL2 REQUIRED sdl2)
target_include_directories(xemu-dashboard PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(xemu-dashboard PUBLIC ${SDL2_LIBRARIES})
target_link_directories(xemu-dashboard PUBLIC ${SDL2_LIBRARY_DIRS})

# Bring in mbed TLS support
message(STATUS "Downloading Mbed TLS")
FetchContent_Declare(
  mbedtls
  GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
  GIT_TAG        v3.6.4
  GIT_PROGRESS TRUE
)
set(CMAKE_C_SIMULATE_ID FALSE CACHE INTERNAL "Clang" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "Disable mbedtls programs")
set(ENABLE_TESTING OFF CACHE BOOL "Disable Mbed TLS tests")
set(UNSAFE_BUILD OFF CACHE BOOL "Disable unsafe build options")
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL OFF CACHE BOOL "Disable package config and install targets")
add_definitions(
  -DMBEDTLS_CHECK_RETURN=
  -DMBEDTLS_CONFIG_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/lib/mbedtls/my_mbedtls_config.h\"
)
FetchContent_MakeAvailable(mbedtls)
target_include_directories(mbedx509 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/mbedtls)
target_link_libraries(xemu-dashboard PUBLIC mbedtls ${CMAKE_SOURCE_DIR}/lib/mbedtls/ws2_32.lib)

# Bring in llhttp support
# llhttp is a HTTP parser used for handling HTTP requests in the dashboard.
message(STATUS "Downloading llhttp")
FetchContent_Declare(llhttp
  URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.3.0.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")
FetchContent_MakeAvailable(llhttp)
target_compile_options(llhttp_static PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-w>
  )
target_link_libraries(xemu-dashboard PUBLIC llhttp_static)

# Bring in crpyto functions to support SHA1 and RC4
add_subdirectory(lib/xbox_eeprom)
target_link_libraries(xemu-dashboard PUBLIC xbox-eeprom)

# Bring in JSON parser support
add_library(json STATIC
    lib/json/json.c
    lib/json/json.h
)
target_link_libraries(xemu-dashboard PUBLIC json)

# Bring in the git tag
execute_process(
  COMMAND git describe --tags --dirty --always
  OUTPUT_VARIABLE GIT_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_compile_definitions(GIT_VERSION="${GIT_VERSION}")

#Pre-build commands

#Let's download the latest CA certificates
message(STATUS "Downloading CA certificates...")
file(DOWNLOAD
    https://curl.se/ca/cacert.pem
    ${CMAKE_SOURCE_DIR}/assets/cacert.pem
    SHOW_PROGRESS
    STATUS download_status
)

#Post-build commands
add_xbox_build_steps(xemu-dashboard ${XBE_TITLE} ${XBOX_ISO_DIR})
