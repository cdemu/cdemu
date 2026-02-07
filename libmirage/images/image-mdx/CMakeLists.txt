set(image_short "mdx")
set(image_name "image-${image_short}")

project(${image_name} LANGUAGES C)

# Dependencies
find_package(ZLIB 1.2.4 QUIET)
pkg_check_modules(LIBGCRYPT libgcrypt IMPORTED_TARGET)

# Build
if(ZLIB_FOUND AND LIBGCRYPT_FOUND AND (GLIB_glib-2.0_VERSION VERSION_GREATER_EQUAL "2.58"))
    add_library(${image_name} MODULE
        crypto.c
        fragment.c
        gf128mul.c
        parser.c
        plugin.c
    )
    target_link_libraries(${image_name} PRIVATE mirage)
    target_link_libraries(${image_name} PRIVATE PkgConfig::LIBGCRYPT)
    target_link_libraries(${image_name} PRIVATE ZLIB::ZLIB)

    # Disable library prefix
    set_target_properties(${image_name} PROPERTIES PREFIX "")

    # Install
    install(TARGETS ${image_name} DESTINATION ${MIRAGE_PLUGIN_DIR})

    # Install MIME type
    intltool_merge("-x" ${CMAKE_SOURCE_DIR}/po ${PROJECT_SOURCE_DIR}/libmirage-${image_short}.xml.in libmirage-${image_short}.xml)

    install(FILES ${PROJECT_BINARY_DIR}/libmirage-${image_short}.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    if(POST_INSTALL_HOOKS)
        install(CODE "execute_process (COMMAND ${UPDATE_MIME_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/mime)")
    endif()

    # Add to list of enabled image formats
    list(APPEND IMAGE_FORMATS_ENABLED ${image_short})
    set(IMAGE_FORMATS_ENABLED ${IMAGE_FORMATS_ENABLED} PARENT_SCOPE)
else()
    # Add to list of disabled image formats
    list(APPEND IMAGE_FORMATS_DISABLED ${image_short})
    set(IMAGE_FORMATS_DISABLED ${IMAGE_FORMATS_DISABLED} PARENT_SCOPE)
endif()
