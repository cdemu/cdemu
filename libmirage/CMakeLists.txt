cmake_minimum_required (VERSION 2.8.5)

# Project name and version
project (libmirage C)
set (PROJECT_VERSION 1.6.0)

# libMirage release and libtool versioning:
set (MIRAGE_MAJOR_VERSION 1)
set (MIRAGE_MINOR_VERSION 6)
set (MIRAGE_MICRO_VERSION 0)
set (MIRAGE_VERSION_LONG 1.6.0)
set (MIRAGE_VERSION_SHORT 1.6)
# Increment if the interface has additions, changes, removals.
set (MIRAGE_LT_CURRENT 7)
# Increment any time the source changes; set to 0 if you increment CURRENT
set (MIRAGE_LT_REVISION 0)
# Increment if any interfaces have been added; set to 0 if any interfaces have
# been removed. Removal has precedence over adding, so set to 0 if both happened.
set (MIRAGE_LT_AGE 0)
set (MIRAGE_SOVERSION ${MIRAGE_LT_CURRENT}.${MIRAGE_LT_REVISION}.${MIRAGE_LT_AGE})

# Search in the `cmake' directory for additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (CheckCSourceCompiles)
include (CheckCCompilerFlag)
include (GNUInstallDirs)

include (GObjectIntrospection)
include (GtkDoc)
include (Utilities)
include (FileList)

set (MIRAGE_INSTALL_PLUGINDIR "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}-${MIRAGE_VERSION_SHORT}"
    CACHE PATH "Path to libMirage plugin directory." FORCE)
set (MIRAGE_PLUGIN_DIR "${CMAKE_INSTALL_PREFIX}/${MIRAGE_INSTALL_PLUGINDIR}")
mark_as_advanced (MIRAGE_INSTALL_PLUGINDIR)

mark_as_advanced (CLEAR CMAKE_INSTALL_SYSCONFDIR)
mark_as_advanced (CLEAR CMAKE_VERBOSE_MAKEFILE)

# Configuration
configure_file (
    ${PROJECT_SOURCE_DIR}/config.h.in
    ${PROJECT_BINARY_DIR}/config.h
)
configure_file (
    ${PROJECT_SOURCE_DIR}/src/libmirage/mirage-version.h.in
    ${PROJECT_SOURCE_DIR}/src/libmirage/mirage-version.h
)
configure_file (
    ${PROJECT_SOURCE_DIR}/docs/reference/libmirage/version.xml.in
    ${PROJECT_SOURCE_DIR}/docs/reference/libmirage/version.xml
)
configure_file (
    ${PROJECT_SOURCE_DIR}/libmirage.pc.in
    ${PROJECT_BINARY_DIR}/libmirage.pc
    @ONLY
)
include_directories (${PROJECT_BINARY_DIR})

set_directory_properties (
    PROPERTY ADDITIONAL_MAKE_CLEAN_FILES 
    "${PROJECT_SOURCE_DIR}/src/libmirage/mirage-version.h;${PROJECT_SOURCE_DIR}/docs/reference/libmirage/version.xml"
)

if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Choose the type of build: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif ()

list (APPEND libmirage_CFLAGS -std=c99)
list (APPEND libmirage_DEFS HAVE_CONFIG_H)

if (STRICT_WARNINGS AND CMAKE_COMPILER_IS_GNUCC)
    list (APPEND libmirage_CFLAGS -Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
    list (APPEND libmirage_DEFS G_DISABLE_DEPRECATED)
    check_c_compiler_flag (-Wno-strict-aliasing CC_HAVE_NSA)
    check_c_compiler_flag (-Wno-sign-compare CC_HAVE_NSC)
    if (CC_HAVE_NSA)
        list (APPEND libmirage_CFLAGS -Wno-strict-aliasing)
    endif ()
    if (CC_HAVE_NSC)
        list (APPEND libmirage_CFLAGS -Wno-sign-compare)
    endif ()
endif ()

option (GTKDOC_ENABLED "Use gtk-doc to build documentation" on)
option (INTROSPECTION_ENABLED "Generate gobject introspection files" on)
option (STRICT_WARNINGS "Enable more warnings" off)

# Tests

find_package (PkgConfig 0.16 REQUIRED)

pkg_check_modules (LibGlib REQUIRED glib-2.0>=2.24 gobject-2.0>=2.24 gmodule-2.0>=2.24 gio-2.0>=2.24)
pkg_check_modules (ZLib REQUIRED zlib>=1.2.0)
pkg_check_modules (LibLzma REQUIRED liblzma>=5.0.0)
pkg_check_modules (LibSndFile REQUIRED sndfile>=1.0.0)

if (INTROSPECTION_ENABLED)
    pkg_check_modules (INTROSPECTION REQUIRED gobject-introspection-1.0>=1.30.0)
endif ()

if (GTKDOC_ENABLED)
    pkg_check_modules (GTKDOC REQUIRED gtk-doc>=1.4)
endif ()

list (APPEND libmirage_CFLAGS ${LibGlib_CFLAGS})
list (APPEND libmirage_LDFLAGS ${LibGlib_LDFLAGS})

# Main library source files
list (APPEND mirage_CFLAGS ${libmirage_CFLAGS})
list (APPEND mirage_DEFS ${libmirage_DEFS})
list (APPEND mirage_DEFS MIRAGE_PLUGIN_DIR="${MIRAGE_PLUGIN_DIR}")
list (APPEND mirage_LDFLAGS ${libmirage_LDFLAGS})

to_list_spaces (mirage_CFLAGS mirage_S_CFLAGS)
to_list_spaces (mirage_LDFLAGS mirage_S_LDFLAGS)

include_directories (${PROJECT_SOURCE_DIR}/src/libmirage)

add_library (mirage SHARED ${libmirage_SOURCES})

target_link_libraries (mirage ${LibGlib_LIBRARIES})

set_target_properties (mirage PROPERTIES 
    COMPILE_FLAGS ${mirage_S_CFLAGS}
    COMPILE_DEFINITIONS "${mirage_DEFS}"
    LINK_FLAGS ${mirage_S_LDFLAGS}
    LIBRARY_OUTPUT_NAME mirage
    SOVERSION ${MIRAGE_SOVERSION}
)

install (
    TARGETS mirage
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install (
    FILES ${libmirage_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libmirage
)

# Fragments source files
list (APPEND fragments_CFLAGS ${libmirage_CFLAGS})
list (APPEND fragments_CFLAGS ${LibSndFile_CFLAGS})
list (APPEND fragments_DEFS ${libmirage_DEFS})
list (APPEND fragments_LDFLAGS ${libmirage_LDFLAGS})
list (APPEND fragments_LDFLAGS ${LibSndFile_LDFLAGS})

to_list_spaces (fragments_CFLAGS fragments_S_CFLAGS)
to_list_spaces (fragments_LDFLAGS fragments_S_LDFLAGS)

foreach (fragment ${fragments_LIST})
    add_library (${fragment} MODULE ${${fragment}_SOURCES})

    target_link_libraries (${fragment} ${LibGlib_LIBRARIES} ${LibSndFile_LIBRARIES})

    set_target_properties (${fragment} PROPERTIES 
        COMPILE_FLAGS ${fragments_S_CFLAGS}
        COMPILE_DEFINITIONS "${fragments_DEFS}"
        LINK_FLAGS ${fragments_S_LDFLAGS}
        PREFIX ""
    )

    install (
        TARGETS ${fragment} 
        DESTINATION ${MIRAGE_INSTALL_PLUGINDIR}
    )
endforeach ()

# Filters source files
list (APPEND filters_CFLAGS ${libmirage_CFLAGS})
list (APPEND filters_CFLAGS ${ZLib_CFLAGS})
list (APPEND filters_DEFS ${libmirage_DEFS})
list (APPEND filters_LDFLAGS ${libmirage_LDFLAGS})
list (APPEND filters_LDFLAGS ${ZLib_LDFLAGS})

to_list_spaces (filters_CFLAGS filters_S_CFLAGS)
to_list_spaces (filters_LDFLAGS filters_S_LDFLAGS)

foreach (filter ${filters_LIST})
    add_library (${filter} MODULE ${${filter}_SOURCES})

    target_link_libraries (${filter} ${LibGlib_LIBRARIES} ${ZLib_LIBRARIES})

    if (filter STREQUAL "filter-xz")
        to_list_spaces (LibLzma_CFLAGS extra_CFLAGS)
        to_list_spaces (LibLzma_LDFLAGS extra_LDFLAGS)
        target_link_libraries (${filter} ${LibLzma_LIBRARIES})
    elseif (filter STREQUAL "filter-daa")
        set (extra_CFLAGS -I${PROJECT_SOURCE_DIR}/src/filters/filter-daa/lzma-sdk)
        set (extra_LDFLAGS)
    else ()
        set (extra_CFLAGS)
        set (extra_LDFLAGS)
    endif ()

    set_target_properties (${filter} PROPERTIES 
        COMPILE_FLAGS "${filters_S_CFLAGS} ${extra_CFLAGS}"
        COMPILE_DEFINITIONS "${filters_DEFS}"
        LINK_FLAGS "${filters_S_LDFLAGS} ${extra_LDFLAGS}"
        PREFIX ""
    )

    install (
        TARGETS ${filter} 
        DESTINATION ${MIRAGE_INSTALL_PLUGINDIR}
    )
endforeach ()

# Parsers source files
list (APPEND parsers_CFLAGS ${libmirage_CFLAGS})
list (APPEND parsers_CFLAGS ${ZLib_CFLAGS})
list (APPEND parsers_DEFS ${libmirage_DEFS})
list (APPEND parsers_LDFLAGS ${libmirage_LDFLAGS})
list (APPEND parsers_LDFLAGS ${ZLib_LDFLAGS})

to_list_spaces (parsers_CFLAGS parsers_S_CFLAGS)
to_list_spaces (parsers_LDFLAGS parsers_S_LDFLAGS)

foreach (parser ${parsers_LIST})
    add_library (${parser} MODULE ${${parser}_SOURCES})

    target_link_libraries (${parser} ${LibGlib_LIBRARIES} ${ZLib_LIBRARIES})

    set_target_properties (${parser} PROPERTIES 
        COMPILE_FLAGS ${parsers_S_CFLAGS}
        COMPILE_DEFINITIONS "${parsers_DEFS}"
        LINK_FLAGS ${parsers_S_LDFLAGS}
        PREFIX ""
    )

    install (
        TARGETS ${parser} 
        DESTINATION ${MIRAGE_INSTALL_PLUGINDIR}
    )
endforeach ()

# Mimetype files
install (
    FILES ${mimetype_FILES} 
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
)

# Introspection stuff
if (INTROSPECTION_ENABLED AND INTROSPECTION_FOUND)
    gobject_introspection (
        FILENAME libMirage-1.0.gir
        NSVERSION ${MIRAGE_VERSION_SHORT}
        INCLUDES GObject-2.0 Gio-2.0 GLib-2.0 GModule-2.0
        LIBRARY mirage
        QUIET
        SCANNER_ARGS --add-include-path=${PROJECT_SOURCE_DIR} --warn-all
        COMPILER_ARGS --includedir=${PROJECT_SOURCE_DIR}
        SYMBOL_PREFIXES mirage
        IDENTIFIER_PREFIXES Mirage
        SOURCES ${libmirage_SOURCES} ${libmirage_HEADERS}
    )
endif ()

# Gtk-Doc documentation
if (GTKDOC_ENABLED AND GTKDOC_FOUND)
    get_target_property (GTKDOC_LIB mirage LIBRARY_OUTPUT_NAME)
    gtk_doc (
        MODULE ${PROJECT_NAME}
        MAIN_SGML_FILE ${PROJECT_NAME}-docs.sgml
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/src/libmirage
        SOURCES ${libmirage_HEADERS} ${libmirage_SOURCES}
        DOCS_DIR ${PROJECT_SOURCE_DIR}/docs/reference/libmirage
        IGNORE_HFILES src/libmirage/mirage-disc-structures.h
        CONTENT_FILES version.xml
        CFLAGS ${libmirage_CFLAGS} -I${PROJECT_SOURCE_DIR}/src/libmirage
        LDFLAGS ${libmirage_LDFLAGS} -L${PROJECT_BINARY_DIR} -Wl,-rpath,${PROJECT_BINARY_DIR} -l${GTKDOC_LIB}
    )
endif ()

# Install misc.
install (
    FILES ${PROJECT_BINARY_DIR}/libmirage.pc 
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

