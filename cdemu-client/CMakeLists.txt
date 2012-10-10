cmake_minimum_required (VERSION 2.8.5)

# Project name and version
project (cdemu-client NONE)
set (cdemu-client_NAME "image-analyzer")
set (cdemu-client_VERSION "1.6.0")

include (GNUInstallDirs)

# Configuration

option (BUILD_NLS "Support for multiple languages" on)
 
# Tests

#find_package (PkgConfig 0.16 REQUIRED)
find_package (Gettext REQUIRED)
find_package (PythonInterp 2.6 REQUIRED)

find_program (HAVE_INTLTOOL NAMES intltool-update intltool-extract intltool-merge intltool-prepare)
if (HAVE_INTLTOOL)
    execute_process (
        COMMAND intltool-update --version 
        COMMAND head --lines=1
        COMMAND cut -d " " -f 3
        OUTPUT_VARIABLE INTLTOOL_VERSION
    )
    #TODO: Check if intltool version is >= 0.21
else ()
    message ("You are missing intltool, please install.")
endif ()

# Functions 

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

    set (translations-target "${cdemu-client_NAME}-translations")
    add_custom_target (${translations-target} ALL DEPENDS ${gmo_files})
endfunction ()

function (intltool_merge options po_dir in_filename out_filename)
    if (options EQUAL "-x")
        set (options2 "--no-translations")
    endif ()
    add_custom_command (
        OUTPUT ${out_filename} 
        COMMAND intltool-merge ${options} -u ${PROJECT_SOURCE_DIR}/${po_dir}
            ${PROJECT_SOURCE_DIR}/${in_filename} ${out_filename} 
        DEPENDS ${in_filename}
    )
    set (intltool-merge-target "intltool-merge-${out_filename}")
    add_custom_target (${intltool-merge-target} ALL DEPENDS ${out_filename})
endfunction ()

# Installation

install (
    PROGRAMS src/cdemu 
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install (
    FILES man/cdemu.1 
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

install (
    FILES data/cdemu-bashcomp
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/bash_completion.d
    RENAME cdemu-client
)

intltool_merge ("-d" po data/cdemu-client.desktop.in cdemu-client.desktop)
install (
    FILES ${PROJECT_BINARY_DIR}/cdemu-client.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

if (BUILD_NLS AND GETTEXT_FOUND)
    gettext_process_po_files (${PROJECT_SOURCE_DIR}/po ${cdemu-client_NAME})
endif ()

