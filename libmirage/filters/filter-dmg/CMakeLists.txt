set(filter_short "dmg")
set(filter_name "filter-${filter_short}")

project(${filter_name} LANGUAGES C)

# Dependencies
find_package(ZLIB 1.2.4 QUIET)
find_package(BZip2 1.0.0 QUIET)

# Build
if(ZLIB_FOUND AND BZIP2_FOUND)
    # Filter
    add_library(${filter_name} MODULE
        adc.c
        filter-stream.c
        plugin.c
        resource-fork.c
    )
    target_link_libraries(${filter_name} PRIVATE mirage)
    target_link_libraries(${filter_name} PRIVATE BZip2::BZip2)
    target_link_libraries(${filter_name} PRIVATE ZLIB::ZLIB)

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
