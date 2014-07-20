if (POST_INSTALL_HOOKS)
        install(CODE "execute_process (COMMAND ${UPDATE_MIME_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/mime)")
    endif ()
set(filter_short "daa")
set(filter_name "filter-${filter_short}")

project(${filter_name} C)

# Dependencies
pkg_check_modules(ZLIB zlib>=1.2.0)

# Build
if (ZLIB_FOUND)
    # Include directories
    include_directories(${ZLIB_INCLUDE_DIRS})
    include_directories(lzma-sdk)

    # Link directories
    link_directories(${ZLIB_LIBRARY_DIRS})

    # Filter
    add_library(${filter_name} MODULE
        filter-stream.c
        plugin.c
        lzma-sdk/Bra86.c
        lzma-sdk/LzmaDec.c
    )
    target_link_libraries(${filter_name} ${GLIB_LIBRARIES} ${ZLIB_LIBRARIES})

    # Disable library prefix
    set_target_properties(${filter_name} PROPERTIES PREFIX "")

    # Install
    install(TARGETS ${filter_name} DESTINATION ${MIRAGE_PLUGIN_DIR})

    # Install MIME type
    install(FILES "libmirage-${filter_short}.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES "libmirage-gbi.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    if (POST_INSTALL_HOOKS)
        install(CODE "execute_process (COMMAND ${UPDATE_MIME_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/mime)")
    endif ()

    # Add to list of enabled filters
    list(APPEND FILTERS_ENABLED ${filter_short})
    set(FILTERS_ENABLED ${FILTERS_ENABLED} PARENT_SCOPE)
else ()
    # Add to list of disabled filters
    list(APPEND FILTERS_DISABLED ${filter_short})
    set(FILTERS_DISABLED ${FILTERS_DISABLED} PARENT_SCOPE)
endif ()
