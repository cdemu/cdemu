set(filter_short "lzop")
set(filter_name "filter-${filter_short}")

project(${filter_name} LANGUAGES C)

# Dependencies
pkg_check_modules(LZO lzo2 IMPORTED_TARGET)

# Build
if(LZO_FOUND)
    # Filter
    add_library(${filter_name} MODULE
        filter-stream.c
        plugin.c
    )
    target_link_libraries(${filter_name} PRIVATE mirage)
    target_link_libraries(${filter_name} PRIVATE PkgConfig::LZO)

    # Disable library prefix
    set_target_properties(${filter_name} PROPERTIES PREFIX "")

    # Install
    install(TARGETS ${filter_name} DESTINATION ${MIRAGE_PLUGIN_DIR})

    # Add to list of enabled filters
    list(APPEND FILTERS_ENABLED ${filter_short})
    set(FILTERS_ENABLED ${FILTERS_ENABLED} PARENT_SCOPE)
else()
    # Add to list of disabled filters
    list(APPEND FILTERS_DISABLED ${filter_short})
    set(FILTERS_DISABLED ${FILTERS_DISABLED} PARENT_SCOPE)
endif()
