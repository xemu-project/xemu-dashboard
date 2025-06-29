if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "CMAKE_TOOLCHAIN_FILE must be set to the NXDK toolchain")
endif()

get_filename_component(ABS_TOOLCHAIN_PATH "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE)
get_filename_component(NXDK_SHARE_DIR "${ABS_TOOLCHAIN_PATH}" DIRECTORY)
get_filename_component(NXDK_SOURCE_DIR "${NXDK_SHARE_DIR}" DIRECTORY)

if (NOT EXISTS "${NXDK_SOURCE_DIR}/lib/libnxdk.lib" OR NOT EXISTS "${NXDK_SOURCE_DIR}/tools/extract-xiso/build/extract-xiso")
    message(STATUS "Bootstrapping NXDK build in ${NXDK_SOURCE_DIR}.")

    find_program(MAKE_COMMAND NAMES gmake make REQUIRED)

    set(NXDK_DEBUG_FLAG "n" CACHE STRING "Enable debug build for the NXDK submodule. 'y' or 'n'.")

    set(
            NXDK_BUILD_COMMAND
            "
                export NXDK_DIR=\"${NXDK_SOURCE_DIR}\"
                cd bin &&
                . activate -s &&
                cd .. &&
                DEBUG=${NXDK_DEBUG_FLAG} NXDK_ONLY=y ${MAKE_COMMAND} -j &&
                ${MAKE_COMMAND} tools -j
            "
    )

    execute_process(
            COMMAND /bin/bash -c "${NXDK_BUILD_COMMAND}"
            WORKING_DIRECTORY "${NXDK_SOURCE_DIR}"
            RESULT_VARIABLE nxdk_build_result
            OUTPUT_VARIABLE nxdk_build_output
            ERROR_VARIABLE nxdk_build_output
    )

    if (NOT nxdk_build_result EQUAL 0)
        message(FATAL_ERROR "Failed to build the nxdk submodule. Error code: ${nxdk_build_result}\nOutput:\n${nxdk_build_output}")
    else ()
        message(STATUS "NXDK bootstrap build completed successfully.")
    endif ()

endif ()
