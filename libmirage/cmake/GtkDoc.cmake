#
# Function for generating Gtk-Doc documentation
#
# Author: Henrik Stokseth
#

include (CMakeParseArguments)
include (GNUInstallDirs)

find_package (PkgConfig REQUIRED QUIET)

pkg_check_modules (GTKDOC REQUIRED QUIET gtk-doc)

# Converts a CMake list to a string containing elements separated by spaces
function (TO_LIST_SPACES LIST_NAME OUTPUT_VAR)
    set (NEW_LIST_SPACE)
    foreach (ITEM ${${LIST_NAME}})
        set (NEW_LIST_SPACE "${NEW_LIST_SPACE} ${ITEM}")
    endforeach ()
    string (STRIP "${NEW_LIST_SPACE}" NEW_LIST_SPACE)
    set (${OUTPUT_VAR} "${NEW_LIST_SPACE}" PARENT_SCOPE)
endfunction ()

function (gtk_doc)
    set (options QUIET VERBOSE)
    set (oneValueArgs
        MODULE
        MAIN_SGML_FILE
        DOCS_DIR
    )
    set (multiValueArgs
        SOURCES
        SOURCE_DIR
        IGNORE_HFILES
        CFLAGS
        LDFLAGS
        CONTENT_FILES
        EXPAND_CONTENT_FILES
    )

    CMAKE_PARSE_ARGUMENTS (GTKDOC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (GTKDOC_UNPARSED_ARGUMENTS)
        message (FATAL_ERROR "Unknown keys given to GTK_DOC(): \"${GTKDOC_UNPARSED_ARGUMENTS}\"")
    endif (GTKDOC_UNPARSED_ARGUMENTS)

    # setup build
    set (SETUP_FILES
        ${GTKDOC_CONTENT_FILES}
        ${GTKDOC_EXPAND_CONTENT_FILES}
        ${GTKDOC_MAIN_SGML_FILE}
        ${GTKDOC_MODULE}-sections.txt
        ${GTKDOC_MODULE}-overrides.txt
        ${GTKDOC_MODULE}.types
    )

    string (REPLACE "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}"
        GTKDOC_DOCS_BUILDDIR "${GTKDOC_DOCS_DIR}")

    foreach (infile ${SETUP_FILES})
        if (EXISTS ${GTKDOC_DOCS_DIR}/${infile})
            file (
                COPY ${GTKDOC_DOCS_DIR}/${infile}
                DESTINATION ${GTKDOC_DOCS_BUILDDIR}
            )
        endif ()
    endforeach ()

    # prepare source dirs
    foreach (dir ${GTKDOC_SOURCE_DIR})
        list (APPEND GTKDOC_SOURCE_DIR_LIST --source-dir=${dir})
    endforeach ()

    # scan header files and introspect gobjects
    to_list_spaces (GTKDOC_CFLAGS GTKDOC_S_CFLAGS)
    to_list_spaces (GTKDOC_LDFLAGS GTKDOC_S_LDFLAGS)
    file (
        WRITE ${PROJECT_BINARY_DIR}/run-scangobj
        "#!/bin/sh\n"
        "export CFLAGS=\"${GTKDOC_S_CFLAGS}\"\n"
        "export LDFLAGS=\"${GTKDOC_S_LDFLAGS}\"\n"
        "gtkdoc-scangobj --module=\"${GTKDOC_MODULE}\"\n"
    )
    add_custom_command (
        OUTPUT ${GTKDOC_DOCS_BUILDDIR}/scan.stamp
        COMMAND gtkdoc-scan --module=${GTKDOC_MODULE} ${GTKDOC_SOURCE_DIR_LIST}
            --ignore-headers=${GTKDOC_IGNORE_HFILES}
        COMMAND sh ${PROJECT_BINARY_DIR}/run-scangobj
        COMMAND touch ${GTKDOC_DOCS_BUILDDIR}/scan.stamp
        WORKING_DIRECTORY ${GTKDOC_DOCS_BUILDDIR}
        DEPENDS mirage ${GTKDOC_SOURCES}
        VERBATIM
    )

    # build xml
    add_custom_command (
        OUTPUT ${GTKDOC_DOCS_BUILDDIR}/sgml.stamp
        COMMAND gtkdoc-mkdb --module=${GTKDOC_MODULE} --sgml-mode --output-format=xml
            --main-sgml-file=${GTKDOC_MAIN_SGML_FILE} ${GTKDOC_SOURCE_DIR_LIST}
        WORKING_DIRECTORY ${GTKDOC_DOCS_BUILDDIR}
        DEPENDS ${GTKDOC_DOCS_BUILDDIR}/scan.stamp
        VERBATIM
    )

    # build html and fix cross-references
    add_custom_command (
        OUTPUT ${GTKDOC_DOCS_BUILDDIR}/html.stamp
        COMMAND mkdir -p html
        COMMAND cd html && gtkdoc-mkhtml ${GTKDOC_MODULE} ../${GTKDOC_MAIN_SGML_FILE} && cd ..
        COMMAND gtkdoc-fixxref --module=${GTKDOC_MODULE} --module-dir=html
        WORKING_DIRECTORY ${GTKDOC_DOCS_BUILDDIR}
        DEPENDS ${GTKDOC_DOCS_BUILDDIR}/sgml.stamp
        VERBATIM
    )

    # Actual target
    add_custom_target ("gtkdoc-${GTKDOC_MODULE}" ALL
        DEPENDS ${GTKDOC_DOCS_BUILDDIR}/scan.stamp
        ${GTKDOC_DOCS_BUILDDIR}/sgml.stamp
        ${GTKDOC_DOCS_BUILDDIR}/html.stamp)

    install (
        CODE "file (GLOB HTML_FILES \"${GTKDOC_DOCS_BUILDDIR}/html/*\")"
        CODE "file (INSTALL \${HTML_FILES} DESTINATION \"${CMAKE_INSTALL_FULL_DATADIR}/gtk-doc/html/${GTKDOC_MODULE}\")"
    )

endfunction (gtk_doc)

