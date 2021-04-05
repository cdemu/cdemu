set(filter_short "xz")
set(filter_name "filter-${filter_short}")

project(${filter_name} LANGUAGES C)

# Dependencies
pkg_check_modules(LIBLZMA liblzma>=5.0.0 IMPORTED_TARGET)

# Build
if(LIBLZMA_FOUND)
    # Filter
    add_library(${filter_name} MODULE
        filter-stream.c
        plugin.c
    )
    target_link_libraries(${filter_name} PRIVATE mirage)
    target_link_libraries(${filter_name} PRIVATE PkgConfig::LIBLZMA)

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
