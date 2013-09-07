set(parser_short "readcd")
set(parser_name "image-${parser_short}")

project(${parser_name} C)

# Build
if (TRUE)
    # Parser
    add_library(${parser_name} MODULE
        ${parser_name}-parser.c
        ${parser_name}-plugin.c 
    )
    target_link_libraries(${parser_name} ${GLIB_LIBRARIES})

    # Disable library prefix
    set_target_properties(${parser_name} PROPERTIES PREFIX "")

    # Install
    install(TARGETS ${parser_name} DESTINATION ${MIRAGE_PLUGIN_DIR})
    
    # Add to list of enabled parsers
    set(PARSERS_ENABLED "${PARSERS_ENABLED} ${parser_short}" PARENT_SCOPE)
else ()
    # Add to list of disabled parsers
    set(PARSERS_DISABLED "${PARSERS_DISABLED} ${parser_short}" PARENT_SCOPE)
endif ()
