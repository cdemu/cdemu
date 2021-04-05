set(filter_short "ecm")
set(filter_name "filter-${filter_short}")

project(${filter_name} LANGUAGES C)

# Build
if(TRUE)
    # Filter
    add_library(${filter_name} MODULE
        filter-stream.c
        plugin.c
    )
    target_link_libraries(${filter_name} mirage)

    # Disable library prefix
    set_target_properties(${filter_name} PROPERTIES PREFIX "")

    # Install
    install(TARGETS ${filter_name} DESTINATION ${MIRAGE_PLUGIN_DIR})

    # Install MIME type
    intltool_merge("-x" ${CMAKE_SOURCE_DIR}/po ${PROJECT_SOURCE_DIR}/libmirage-${filter_short}.xml.in libmirage-${filter_short}.xml)

    install(FILES ${PROJECT_BINARY_DIR}/libmirage-${filter_short}.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    if(POST_INSTALL_HOOKS)
        install(CODE "execute_process (COMMAND ${UPDATE_MIME_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/mime)")
    endif()

    # Add to list of enabled filters
    list(APPEND FILTERS_ENABLED ${filter_short})
    set(FILTERS_ENABLED ${FILTERS_ENABLED} PARENT_SCOPE)
else()
    # Add to list of disabled filters
    list(APPEND FILTERS_DISABLED ${filter_short})
    set(FILTERS_DISABLED ${FILTERS_DISABLED} PARENT_SCOPE)
endif()
