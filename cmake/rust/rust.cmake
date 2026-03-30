# Put the resulting file in the top-level build directory so that it can be easily accessed without CMake
set(RUST_LINK_DEPENDENCIES_FILE ${CMAKE_BINARY_DIR}/everestrs-link-dependencies.txt)
set(RUST_LINK_DEPENDENCIES "$<TARGET_GENEX_EVAL:everest::everestrs_sys,$<TARGET_PROPERTY:everest::everestrs_sys,EVERESTRS_LINK_DEPENDENCIES>>")
set(RUST_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/everestrs)
find_program(CARGO_EXECUTABLE NAMES cargo REQUIRED)

add_custom_command(
    OUTPUT
        ${RUST_LINK_DEPENDENCIES_FILE}
    COMMAND
        ${CMAKE_COMMAND} -E make_directory "${RUST_OUTPUT_DIR}"
    COMMAND
        echo -e $<LIST:JOIN,${RUST_LINK_DEPENDENCIES},\\n> > "${RUST_LINK_DEPENDENCIES_FILE}"
    VERBATIM
    COMMAND_EXPAND_LISTS
)

add_custom_target(generate_rust
    DEPENDS
        everestrs_sys
        ${RUST_WORKSPACE_CARGO_FILE}
        ${RUST_LINK_DEPENDENCIES_FILE}
)

find_package(Python3 COMPONENTS Interpreter)
if(NOT ${Python3_Interpreter_FOUND})
    message(FATAL_ERROR "python3 is required to build Rust modules")
endif()
set(GENERATE_CARGO_CONFIG_SCRIPT "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate-cargo-config.py")

if(USING_MUSL)
    set(RUST_TARGET_TRIPLE "${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-musl")
else()
    set(RUST_TARGET_TRIPLE "${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-gnu")
endif()

# Store variables as target properties so that that are accessible in different scopes
set_target_properties(generate_rust
    PROPERTIES
        CARGO_EXECUTABLE "${CARGO_EXECUTABLE}"
        RUST_OUTPUT_DIR "${RUST_OUTPUT_DIR}"
        RUST_LINK_DEPENDENCIES_FILE "${RUST_LINK_DEPENDENCIES_FILE}"
        RUST_TARGET_TRIPLE "${RUST_TARGET_TRIPLE}"
        EVEREST_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
        GENERATE_CARGO_CONFIG_SCRIPT "${GENERATE_CARGO_CONFIG_SCRIPT}"
        ADDITIONAL_CLEAN_FILES "${RUST_OUTPUT_DIR}/target"
)

# FIXME (aw): we might also look for a CMakeFiles.txt in the module folder for custom logic
function (ev_add_rs_module MODULE_NAME)
    if(NOT ${EVEREST_ENABLE_RS_SUPPORT})
        message(STATUS "Excluding Rust module ${MODULE_NAME} because EVEREST_ENABLE_RS_SUPPORT=${EVEREST_ENABLE_RS_SUPPORT}")
        return()
    elseif ("${MODULE_NAME}" IN_LIST EVEREST_EXCLUDE_MODULES)
        message(STATUS "Excluding module ${MODULE_NAME}")
        return()
    elseif (EVEREST_INCLUDE_MODULES AND NOT ("${MODULE_NAME}" IN_LIST EVEREST_INCLUDE_MODULES))
        message(STATUS "Excluding module ${MODULE_NAME}")
        return()
    endif ()

    set(MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_NAME}")
    if (NOT IS_DIRECTORY ${MODULE_PATH})
        message(FATAL "Rust module ${MODULE_NAME} does not exist at ${MODULE_PATH}")
        return()
    endif ()

    get_target_property(RUST_OUTPUT_DIR generate_rust RUST_OUTPUT_DIR)
    get_target_property(RUST_TARGET_TRIPLE generate_rust RUST_TARGET_TRIPLE)
    set(RUST_MODULE_OUTPUT_DIR ${RUST_OUTPUT_DIR}/modules/${MODULE_NAME})
    set(RUST_MODULE_ARTIFACT_DIR ${RUST_MODULE_OUTPUT_DIR}/artifacts/$<CONFIG>)
    set(RUST_MODULE_BINARY ${RUST_MODULE_ARTIFACT_DIR}/bin/${MODULE_NAME})
    set(CARGO_CONFIG_FILE ${RUST_MODULE_OUTPUT_DIR}/cargo-config.toml)
    set(RUST_TARGET_BINARY_DIR ${RUST_OUTPUT_DIR}/target/${RUST_TARGET_TRIPLE}/$<LOWER_CASE:$<CONFIG>>)

    message(STATUS "Setting up Rust module ${MODULE_NAME}")

    add_custom_command(
        OUTPUT
            ${CARGO_CONFIG_FILE}
        WORKING_DIRECTORY
            ${MODULE_PATH} # This makes Cargo respect the module's `.cargo/config.toml` (if present)
        COMMAND
            ${CMAKE_COMMAND} -E make_directory "${RUST_MODULE_OUTPUT_DIR}"
        COMMAND
            $<TARGET_PROPERTY:generate_rust,GENERATE_CARGO_CONFIG_SCRIPT>
                --cargo "$<TARGET_PROPERTY:generate_rust,CARGO_EXECUTABLE>"
                --target "${RUST_TARGET_TRIPLE}"
                --manifest-path "${MODULE_PATH}/Cargo.toml"
                --output "${CARGO_CONFIG_FILE}"
                "everestrs=$<TARGET_PROPERTY:everest::everestrs_sys,EVERESTRS_DIR>"
                "everestrs-build=$<TARGET_PROPERTY:everest::everestrs_sys,EVERESTRS_BUILD_DIR>"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.20")
        # Relative paths in depfiles used to be relative to the top-level build directory, but are now relative to the current working directory.
        # This is irrelevant for us as we only use absolute paths, but CMake will warn about it unless we set this policy.
        cmake_policy(SET CMP0116 NEW)
    endif()

    if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.25")
        # When the installation prefix is not in $PATH `cargo install` prints a warning, which this silences.
        # Cargo doesn't (yet) support doing so directly: https://github.com/rust-lang/cargo/issues/14276
        set(CARGO_EXTRA_ENV_FLAGS --modify PATH=path_list_append:${RUST_MODULE_ARTIFACT_DIR}/bin --)
    else()
        set(CARGO_EXTRA_ENV_FLAGS "")
    endif()

    add_custom_command(
        OUTPUT
            ${RUST_MODULE_BINARY}
            ${RUST_TARGET_BINARY_DIR}/${MODULE_NAME}
        DEPENDS
            generate_rust
            ${CARGO_CONFIG_FILE}
        WORKING_DIRECTORY
            ${MODULE_PATH} # This makes Cargo respect the module's `.cargo/config.toml` (if present)
        COMMAND
            ${CMAKE_COMMAND} -E env
                EVEREST_CORE_ROOT=$<TARGET_PROPERTY:generate_rust,EVEREST_CORE_ROOT>
                EVEREST_RS_LINK_DEPENDENCIES=$<TARGET_PROPERTY:generate_rust,RUST_LINK_DEPENDENCIES_FILE>
                CARGO_TARGET_DIR="${RUST_OUTPUT_DIR}/target"
                # CMake may be using a toolchain file configured to use a different C/C++ compiler than the environment specifies,
                # which should be propagated to Cargo so it can compile FFI bindings correctly in build scripts. Important for cross-compilation.
                CC=${CMAKE_C_COMPILER}
                CXX=${CMAKE_CXX_COMPILER}
                ${CARGO_EXTRA_ENV_FLAGS}
            $<TARGET_PROPERTY:generate_rust,CARGO_EXECUTABLE>
                install # We use `cargo install` instead of the regular `build` because it allows us to patch dependencies' sources without modifying the user's lockfile.
                --path "${MODULE_PATH}"
                --bin ${MODULE_NAME}
                --locked
                --root "${RUST_MODULE_ARTIFACT_DIR}"
                # Explicitly set the linker to match what we're using for C++ to avoid the following issue when cross compiling:
                # https://github.com/rust-lang/rust/issues/28924
                --config "target.${RUST_TARGET_TRIPLE}.linker = \"${CMAKE_CXX_COMPILER}\""
                --config "${CARGO_CONFIG_FILE}"
                --target ${RUST_TARGET_TRIPLE}
                --target-dir "${RUST_OUTPUT_DIR}/target"
                --profile $<IF:$<STREQUAL:$<LOWER_CASE:$<CONFIG>>,debug>,dev,$<LOWER_CASE:$<CONFIG>>>
        DEPFILE
            # List of all source files that were used to build the binary, so that CMake knows when to re-run the target. Generated by Cargo.
            ${RUST_TARGET_BINARY_DIR}/${MODULE_NAME}.d
        VERBATIM
        COMMAND_EXPAND_LISTS
        USES_TERMINAL
        COMMENT "Building ${MODULE_NAME}"
    )

    add_custom_target(${MODULE_NAME} ALL
        DEPENDS ${RUST_MODULE_BINARY}
    )

    set(EVEREST_MODULE_INSTALL_PREFIX "${CMAKE_INSTALL_LIBEXECDIR}/everest/modules")
    install(PROGRAMS ${RUST_MODULE_BINARY}
        DESTINATION "${EVEREST_MODULE_INSTALL_PREFIX}/${MODULE_NAME}"
    )

    # FIXME (aw): this should go into a general function for all add_module_* flavours
    install(FILES ${MODULE_PATH}/manifest.yaml
        DESTINATION "${EVEREST_MODULE_INSTALL_PREFIX}/${MODULE_NAME}"
    )
endfunction()
