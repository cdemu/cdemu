set(filter_short "sndfile")
set(filter_name "filter-${filter_short}")

project(${filter_name} C)

# Dependencies
pkg_check_modules(SNDFILE sndfile>=1.0.0)
pkg_check_modules(SAMPLERATE samplerate>=0.1.0)

# Build
if (${SNDFILE_FOUND} AND ${SAMPLERATE_FOUND})
    # Include directories
    include_directories(${SNDFILE_INCLUDE_DIRS})
    include_directories(${SAMPLERATE_INCLUDE_DIRS})

    # Link directories
    link_directories(${SNDFILE_LIBRARY_DIRS})
    link_directories(${SAMPLERATE_LIBRARY_DIRS})

    # Filter
    add_library(${filter_name} MODULE
        ${filter_name}-filter-stream.c
        ${filter_name}-plugin.c
    )
    target_link_libraries(${filter_name} ${GLIB_LIBRARIES} ${SNDFILE_LIBRARIES} ${SAMPLERATE_LIBRARIES})

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
