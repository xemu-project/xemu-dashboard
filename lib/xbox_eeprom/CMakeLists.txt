cmake_minimum_required(VERSION 3.5)
project(xbox-eeprom LANGUAGES C)

# Define your library
add_library(xbox-eeprom
    rc4.c
    sha1.c
    xbox_eeprom.c
)

target_include_directories(xbox-eeprom PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
