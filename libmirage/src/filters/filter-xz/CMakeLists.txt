set(filter_short "xz")
set(filter_name "filter-${filter_short}")

project(${filter_name} C)

# Dependencies
pkg_check_modules(LIBLZMA liblzma>=5.0.0)

# Build
if (${LIBLZMA_FOUND})
    # Include directories
    include_directories(${LIBLZMA_INCLUDE_DIRS})
    
    # Link directories
    link_directories(${LIBLZMA_LIBRARY_DIRS})

    # Filter
    add_library(${filter_name} MODULE 
        ${filter_name}-file-filter.c
        ${filter_name}-plugin.c
    )
    target_link_libraries(${filter_name} ${GLIB_LIBRARIES} ${LIBLZMA_LIBRARIES})

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
