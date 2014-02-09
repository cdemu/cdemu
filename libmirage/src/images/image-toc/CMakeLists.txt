set(image_short "toc")
set(image_name "image-${image_short}")

project(${image_name} C)

# Build
if (TRUE)
    # Parser
    add_library(${image_name} MODULE
        ${image_name}-parser.c
        ${image_name}-plugin.c
        ${image_name}-writer.c
    )
    target_link_libraries(${image_name} ${GLIB_LIBRARIES})

    # Disable library prefix
    set_target_properties(${image_name} PROPERTIES PREFIX "")

    # Install
    install(TARGETS ${image_name} DESTINATION ${MIRAGE_PLUGIN_DIR})

    # Add to list of enabled parsers
    set(IMAGE_FORMATS_ENABLED "${IMAGE_FORMATS_ENABLED} ${image_short}" PARENT_SCOPE)
else ()
    # Add to list of disabled parsers
    set(IMAGE_FORMATS_DISABLED "${IMAGE_FORMATS_DISABLED} ${image_short}" PARENT_SCOPE)
endif ()
