#
# Cmake include for IntlTool
#
# Author: Henrik Stokseth
#

include (FindPackageHandleStandardArgs)

find_program (INTLTOOL_UPDATE_EXECUTABLE NAMES intltool-update)
find_program (INTLTOOL_EXTRACT_EXECUTABLE NAMES intltool-extract)
find_program (INTLTOOL_MERGE_EXECUTABLE NAMES intltool-merge)
find_program (INTLTOOL_PREPARE_EXECUTABLE NAMES intltool-prepare)

mark_as_advanced (INTLTOOL_UPDATE_EXECUTABLE)
mark_as_advanced (INTLTOOL_EXTRACT_EXECUTABLE)
mark_as_advanced (INTLTOOL_MERGE_EXECUTABLE)
mark_as_advanced (INTLTOOL_PREPARE_EXECUTABLE)

if (INTLTOOL_UPDATE_EXECUTABLE)
    execute_process (
        COMMAND ${INTLTOOL_UPDATE_EXECUTABLE} --version 
        COMMAND head --lines=1
        COMMAND cut -d " " -f 3
        OUTPUT_VARIABLE INTLTOOL_VERSION
    )
endif ()

find_package_handle_standard_args (IntlTool
    REQUIRED_VARS INTLTOOL_UPDATE_EXECUTABLE INTLTOOL_EXTRACT_EXECUTABLE
    INTLTOOL_MERGE_EXECUTABLE INTLTOOL_PREPARE_EXECUTABLE
    VERSION_VAR INTLTOOL_VERSION
)

function (gettext_process_po_files po_dir)
    set (gmo_files)
    file (GLOB po_files ${po_dir}/*.po)

    foreach (po_file ${po_files})
        get_filename_component (lang ${po_file} NAME_WE)
        set (gmo_file ${CMAKE_CURRENT_BINARY_DIR}/${lang}.gmo)
        add_custom_command (
            OUTPUT ${gmo_file}
            COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -o ${gmo_file} ${po_file} 
            DEPENDS ${po_file}
        )
        install (
            FILES ${gmo_file} 
            DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${lang}/LC_MESSAGES 
            RENAME ${package_name}.mo
        )
        set (gmo_files ${gmo_files} ${gmo_file})
    endforeach ()

    set (translations-target "${PROJECT_NAME}-translations")
    add_custom_target (${translations-target} ALL DEPENDS ${gmo_files})
endfunction ()

function (intltool_merge options po_dir in_filename out_filename)
    if (options EQUAL "-x")
        set (options2 "--no-translations")
    endif ()
    add_custom_command (
        OUTPUT ${out_filename}
        COMMAND intltool-merge ${options} ${options2} -u ${PROJECT_SOURCE_DIR}/${po_dir}
            ${PROJECT_SOURCE_DIR}/${in_filename} ${out_filename} 
        DEPENDS ${in_filename}
    )
    set (intltool-merge-target "intltool-merge-${out_filename}")
    add_custom_target (${intltool-merge-target} ALL DEPENDS ${out_filename})
endfunction ()

