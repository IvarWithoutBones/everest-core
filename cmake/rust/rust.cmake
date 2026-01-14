find_program(CARGO_EXECUTABLE cargo REQUIRED)

# FIXME (aw): the RUST_WORKSPACE_DIR could be user setable!
set(RUST_WORKSPACE_DIR ${PROJECT_BINARY_DIR}/rust_workspace)
set(RUST_WORKSPACE_CARGO_FILE ${RUST_WORKSPACE_DIR}/Cargo.toml)

if (NOT EXISTS ${RUST_WORKSPACE_DIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${RUST_WORKSPACE_DIR}")
    message(STATUS "Creating rust workspace at ${RUST_WORKSPACE_DIR}")
endif ()

if (EVEREST_CORE_BUILD_TESTING)
    set(EVERESTRS_FEATURE_FLAGS ",features = [\"link_gcov\"]")
else()
    set(EVERESTRS_FEATURE_FLAGS "")
endif()

# NOTE (aw): we could also write a small python script, which would do that for us
add_custom_command(OUTPUT ${RUST_WORKSPACE_CARGO_FILE}
    COMMAND
        echo "[workspace]" > Cargo.toml
    COMMAND
        echo "resolver = \"2\"" >> Cargo.toml
    COMMAND
        echo "members = [" >> Cargo.toml
    COMMAND
        echo "  \"$<JOIN:$<TARGET_PROPERTY:generate_rust,RUST_MODULE_LIST>,\", \">\"," >> Cargo.toml  # :)
    COMMAND
        echo "]" >> Cargo.toml && echo "" >> Cargo.toml
    COMMAND
        echo "[workspace.dependencies]" >> Cargo.toml
    COMMAND
        echo "everestrs = { path = \"$<TARGET_PROPERTY:everest::everestrs_sys,EVERESTRS_DIR>\" ${EVERESTRS_FEATURE_FLAGS} }" >> Cargo.toml
    COMMAND
        echo "everestrs-build = { path = \"$<TARGET_PROPERTY:everest::everestrs_sys,EVERESTRS_BUILD_DIR>\" }" >> Cargo.toml
    WORKING_DIRECTORY
        ${RUST_WORKSPACE_DIR}
    VERBATIM
    DEPENDS
        ${RUST_WORKSPACE_DIR}
)

# Put the resulting file in the top-level build directory so that it can be easily accessed without CMake
set(RUST_LINK_DEPENDENCIES_FILE ${CMAKE_BINARY_DIR}/everestrs-link-dependencies.txt)
set(RUST_LINK_DEPENDENCIES "$<TARGET_GENEX_EVAL:everest::everestrs_sys,$<TARGET_PROPERTY:everest::everestrs_sys,EVERESTRS_LINK_DEPENDENCIES>>")

add_custom_command(OUTPUT ${RUST_LINK_DEPENDENCIES_FILE}
    COMMAND_EXPAND_LISTS
    VERBATIM
    COMMAND
        echo -e $<LIST:JOIN,${RUST_LINK_DEPENDENCIES},\\n> > "${RUST_LINK_DEPENDENCIES_FILE}"
)

add_custom_target(generate_rust
    DEPENDS
        ${RUST_WORKSPACE_CARGO_FILE}
        ${RUST_LINK_DEPENDENCIES_FILE}
)

# Store the workspace directory as a target property so that it is accessible in different scopes
set_property(TARGET generate_rust
    PROPERTY
        RUST_WORKSPACE_DIR "${RUST_WORKSPACE_DIR}"
)

# FIXME (aw): use generator expressions here, but this first needs to be fixed in the build.rs file ...
add_custom_target(build_rust_modules ALL
    USES_TERMINAL
    COMMENT
        "Build rust modules"
    COMMAND
        ${CMAKE_COMMAND} -E env
        EVEREST_CORE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
        EVEREST_RS_LINK_DEPENDENCIES="${RUST_LINK_DEPENDENCIES_FILE}"
        ${CARGO_EXECUTABLE} build
        $<IF:$<STREQUAL:$<CONFIG>,Release>,--release,>
        # explicitly set the linker to match what we're using for C++ to avoid the following issue when cross compiling:
        # https://github.com/rust-lang/rust/issues/28924
        --config 'target.$<TARGET_PROPERTY:build_rust_modules,RUST_TARGET_TRIPLE>.linker = \"${CMAKE_CXX_COMPILER}\"'
        --target $<TARGET_PROPERTY:build_rust_modules,RUST_TARGET_TRIPLE>
    WORKING_DIRECTORY
        ${RUST_WORKSPACE_DIR}
    DEPENDS
        generate_rust
)

# FIXME: cleaning up doesn't work on the first run
set_property(TARGET build_rust_modules
    APPEND
    PROPERTY
        ADDITIONAL_CLEAN_FILES ${RUST_WORKSPACE_DIR}/target ${RUST_WORKSPACE_DIR}/Cargo.lock
)

set_property(TARGET build_rust_modules
    PROPERTY
        # FIXME: Don't assume the glibc ABI here. This won't respect musl builds.
        RUST_TARGET_TRIPLE "${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-gnu"
)

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

    message(STATUS "Setting up Rust module ${MODULE_NAME}")

    # FIXME (aw): we might also look for a CMakeFiles.txt in the module folder for custom logic
    set_property(
        TARGET generate_rust
        APPEND
        PROPERTY RUST_MODULE_LIST "${MODULE_NAME}"
    )
    get_target_property(RUST_WORKSPACE_DIR generate_rust RUST_WORKSPACE_DIR)

    add_custom_command(OUTPUT ${RUST_WORKSPACE_DIR}/${MODULE_NAME}
        COMMAND
            ${CMAKE_COMMAND} -E create_symlink ${MODULE_PATH} ${MODULE_NAME}
        COMMENT
            "Create symlink for rust module ${MODULE_NAME}"
        VERBATIM
        WORKING_DIRECTORY
            ${RUST_WORKSPACE_DIR}
    )

    add_custom_target(rust_symlink_module_${MODULE_NAME}
        DEPENDS ${RUST_WORKSPACE_DIR}/${MODULE_NAME}
        COMMENT "Create symlink for rust module ${MODULE_NAME}"
    )

    add_dependencies(generate_rust rust_symlink_module_${MODULE_NAME})

    set(EVEREST_MODULE_INSTALL_PREFIX "${CMAKE_INSTALL_LIBEXECDIR}/everest/modules")
    set(BIN_PREFIX "target/$<TARGET_PROPERTY:build_rust_modules,RUST_TARGET_TRIPLE>/$<IF:$<STREQUAL:$<CONFIG>,Release>,release,debug>")

    install(PROGRAMS ${RUST_WORKSPACE_DIR}/${BIN_PREFIX}/${MODULE_NAME}
        DESTINATION "${EVEREST_MODULE_INSTALL_PREFIX}/${MODULE_NAME}"
    )

    # FIXME (aw): this should go into a general function for all add_module_* flavours
    install(FILES ${MODULE_PATH}/manifest.yaml
        DESTINATION "${EVEREST_MODULE_INSTALL_PREFIX}/${MODULE_NAME}"
    )
endfunction()
