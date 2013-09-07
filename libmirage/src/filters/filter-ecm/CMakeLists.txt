set(filter_short "ecm")
set(filter_name "filter-${filter_short}")

project(${filter_name} C)

# Build
if (TRUE)
    # Filter
    add_library(${filter_name} MODULE 
        ${filter_name}-file-filter.c
        ${filter_name}-plugin.c
    )
    target_link_libraries(${filter_name} ${GLIB_LIBRARIES})

    # Disable library prefix
    set_target_properties(${filter_name} PROPERTIES PREFIX "")

    # Install
    install(TARGETS ${filter_name} DESTINATION ${MIRAGE_PLUGIN_DIR})

    # Install MIME type
    install(FILES "libmirage-${filter_short}.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    if (POST_INSTALL_HOOKS)
        install(CODE "execute_process (COMMAND ${UPDATE_MIME_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/mime)")
    endif ()
    
    # Add to list of enabled filters
    set(FILTERS_ENABLED "${FILTERS_ENABLED} ${filter_short}" PARENT_SCOPE)
else ()
    # Add to list of disabled filters
    set(FILTERS_DISABLED "${FILTERS_DISABLED} ${filter_short}" PARENT_SCOPE)
endif ()
