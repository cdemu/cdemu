#
# Function for generating Gtk-Doc documentation
#
# Author: Henrik Stokseth
#

include (CMakeParseArguments)
include (GNUInstallDirs)

include (Utilities)

find_package (PkgConfig REQUIRED QUIET)

pkg_check_modules (GTKDOC REQUIRED QUIET gtk-doc)

function (gtk_doc)
    set (options QUIET VERBOSE)
    set (oneValueArgs
        MODULE
        MAIN_SGML_FILE
        SOURCE_DIR
        DOCS_DIR
    )
    set (multiValueArgs
        SOURCES
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
        OUTPUT ${GTKDOC_DOCS_BUILDDIR}/scan-build.stamp
        COMMAND gtkdoc-scan --module=${GTKDOC_MODULE} --source-dir=${GTKDOC_SOURCE_DIR}
            --ignore-headers="${GTKDOC_IGNORE_HFILES}"
        COMMAND sh ${PROJECT_BINARY_DIR}/run-scangobj
        COMMAND touch ${GTKDOC_DOCS_BUILDDIR}/scan-build.stamp
        WORKING_DIRECTORY ${GTKDOC_DOCS_BUILDDIR}
        DEPENDS mirage ${GTKDOC_SOURCES}
        VERBATIM
    )

    add_custom_target ("gtkdoc-scan ${GTKDOC_MODULE}" ALL
        DEPENDS mirage ${GTKDOC_SOURCES} ${GTKDOC_DOCS_BUILDDIR}/scan-build.stamp)

    # build xml
    add_custom_command (
        OUTPUT ${GTKDOC_DOCS_BUILDDIR}/sgml-build.stamp
        COMMAND gtkdoc-mkdb --module=${GTKDOC_MODULE} --sgml-mode --output-format=xml
            --main-sgml-file=${GTKDOC_MAIN_SGML_FILE} --source-dir=${GTKDOC_SOURCE_DIR}
        COMMAND touch ${GTKDOC_DOCS_BUILDDIR}/sgml-build.stamp
        WORKING_DIRECTORY ${GTKDOC_DOCS_BUILDDIR}
        DEPENDS ${GTKDOC_DOCS_BUILDDIR}/scan-build.stamp
        VERBATIM
    )

    add_custom_target ("gtkdoc-xml ${GTKDOC_MODULE}" ALL
        DEPENDS ${GTKDOC_DOCS_BUILDDIR}/scan-build.stamp
        ${GTKDOC_DOCS_BUILDDIR}/sgml-build.stamp)

    # build html and fix cross-references
    add_custom_command (
        OUTPUT ${GTKDOC_DOCS_BUILDDIR}/html-build.stamp
        COMMAND mkdir html
        COMMAND cd html && gtkdoc-mkhtml ${GTKDOC_MODULE} ../${GTKDOC_MAIN_SGML_FILE}
        COMMAND gtkdoc-fixxref --module=${GTKDOC_MODULE} --module-dir=html
        COMMAND touch ${GTKDOC_DOCS_BUILDDIR}/html-build.stamp
        WORKING_DIRECTORY ${GTKDOC_DOCS_BUILDDIR}
        DEPENDS ${GTKDOC_DOCS_BUILDDIR}/sgml-build.stamp
        VERBATIM
    )

    add_custom_target ("gtkdoc-html ${GTKDOC_MODULE}" ALL
        DEPENDS ${GTKDOC_DOCS_BUILDDIR}/scan-build.stamp
        ${GTKDOC_DOCS_BUILDDIR}/sgml-build.stamp
        ${GTKDOC_DOCS_BUILDDIR}/html-build.stamp)

    install (
        CODE "file (GLOB HTML_FILES \"${GTKDOC_DOCS_BUILDDIR}/html/*\")"
        CODE "file (INSTALL \${HTML_FILES} DESTINATION \"${CMAKE_INSTALL_FULL_DATADIR}/gtk-doc/html/${GTKDOC_MODULE}\")"
    )

endfunction (gtk_doc)

