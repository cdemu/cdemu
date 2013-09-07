set(filter_short "gzip")
set(filter_name "filter-${filter_short}")

project(${filter_name} C)

# Dependencies
pkg_check_modules(ZLIB zlib>=1.2.0)

# Build
if (${ZLIB_FOUND})
    # Include directories
    include_directories(${ZLIB_INCLUDE_DIRS})
    
    # Link directories
    link_directories(${ZLIB_LIBRARY_DIRS})

    # Filter
    add_library(${filter_name} MODULE 
        ${filter_name}-file-filter.c
        ${filter_name}-plugin.c
    )
    target_link_libraries(${filter_name} ${GLIB_LIBRARIES} ${ZLIB_LIBRARIES})

    # Disable library prefix
    set_target_properties(${filter_name} PROPERTIES PREFIX "")

    # Install
    install(TARGETS ${filter_name} DESTINATION ${MIRAGE_PLUGIN_DIR})
    
    # Add to list of enabled filters
    set(FILTERS_ENABLED "${FILTERS_ENABLED} ${filter_short}" PARENT_SCOPE)
else ()
    # Add to list of disabled filters
    set(FILTERS_DISABLED "${FILTERS_DISABLED} ${filter_short}" PARENT_SCOPE)
endif ()
