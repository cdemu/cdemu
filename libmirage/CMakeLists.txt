cmake_minimum_required (VERSION 2.8.5)

# Project name
project (libmirage C)

# Release versioning:
set (MIRAGE_MAJOR_VERSION 2)
set (MIRAGE_MINOR_VERSION 1)
set (MIRAGE_MICRO_VERSION 0)
set (MIRAGE_VERSION_LONG ${MIRAGE_MAJOR_VERSION}.${MIRAGE_MINOR_VERSION}.${MIRAGE_MICRO_VERSION})
set (MIRAGE_VERSION_SHORT ${MIRAGE_MAJOR_VERSION}.${MIRAGE_MINOR_VERSION})

# Libtool versioning:
# Increment if the interface has additions, changes, removals.
set (MIRAGE_LT_CURRENT 8)
# Increment any time the source changes; set to 0 if you increment CURRENT
set (MIRAGE_LT_REVISION 0)
# Increment if any interfaces have been added; set to 0 if any interfaces have
# been removed. Removal has precedence over adding, so set to 0 if both happened.
set (MIRAGE_LT_AGE 1)
set (MIRAGE_SOVERSION ${MIRAGE_LT_CURRENT}.${MIRAGE_LT_REVISION}.${MIRAGE_LT_AGE})

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (CheckCSourceCompiles)
include (CheckCCompilerFlag)
include (GNUInstallDirs)

include (Utilities)
include (FileList)

# Options
option (GTKDOC_ENABLED "Use gtk-doc to build documentation" on)
option (INTROSPECTION_ENABLED "Generate gobject introspection files" on)
option (POST_INSTALL_HOOKS "Run post-install hooks" on)

# Plugin directory
set (MIRAGE_PLUGIN_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/libmirage-${MIRAGE_VERSION_SHORT}" CACHE PATH "Path to libMirage plugin directory." FORCE)

# Dependencies
find_package (PkgConfig 0.16 REQUIRED)
find_package (BZip2 1.0.0 REQUIRED)

pkg_check_modules (GLIB REQUIRED glib-2.0>=2.24 gobject-2.0>=2.24 gmodule-2.0>=2.24 gio-2.0>=2.24)
pkg_check_modules (ZLIB REQUIRED zlib>=1.2.0)
pkg_check_modules (LIBLZMA REQUIRED liblzma>=5.0.0)
pkg_check_modules (SNDFILE REQUIRED sndfile>=1.0.0)
pkg_check_modules (SAMPLERATE REQUIRED samplerate>=0.1.0)

if (INTROSPECTION_ENABLED)
    pkg_check_modules (INTROSPECTION gobject-introspection-1.0>=1.30.0)
    if (INTROSPECTION_FOUND)
        include (GObjectIntrospection)
    else ()
        message (WARNING "Introspection enabled but not found!")
    endif ()
endif ()

if (GTKDOC_ENABLED)
    pkg_check_modules (GTKDOC gtk-doc>=1.4)
    if (GTKDOC_FOUND)
        include (GtkDoc)
    else ()
        message (WARNING "GtkDoc enabled but not found!")
    endif ()
endif ()

if (POST_INSTALL_HOOKS)
    find_program (UPDATE_MIME_DATABASE_EXECUTABLE NAMES update-mime-database)
    mark_as_advanced (UPDATE_MIME_DATABASE_EXECUTABLE)
endif ()

# Auto-generated files
configure_file (
    ${PROJECT_SOURCE_DIR}/config.h.in
    ${PROJECT_BINARY_DIR}/config.h
)
configure_file (
    ${PROJECT_SOURCE_DIR}/src/libmirage/mirage-version.h.in
    ${PROJECT_BINARY_DIR}/src/libmirage/mirage-version.h
)
configure_file (
    ${PROJECT_SOURCE_DIR}/docs/reference/libmirage/version.xml.in
    ${PROJECT_BINARY_DIR}/docs/reference/libmirage/version.xml
)
configure_file (
    ${PROJECT_SOURCE_DIR}/libmirage.pc.in
    ${PROJECT_BINARY_DIR}/libmirage.pc
    @ONLY
)

# Global definitions
add_definitions (-std=c99) # We're compiling using C99
add_definitions (-DHAVE_CONFIG_H)

if (CMAKE_COMPILER_IS_GNUCC)
    add_definitions (-Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
    add_definitions (-Wno-sign-compare)
endif ()

# Global include directories
include_directories (${PROJECT_SOURCE_DIR}/src/libmirage)
include_directories (${PROJECT_BINARY_DIR})
include_directories (${PROJECT_BINARY_DIR}/src/libmirage)


# *** libMirage core library ***
add_library (mirage SHARED ${libmirage_SOURCES})
target_link_libraries (mirage ${GLIB_LIBRARIES})

list (APPEND mirage_CFLAGS ${GLIB_CFLAGS})
list (APPEND mirage_DEFS MIRAGE_PLUGIN_DIR="${MIRAGE_PLUGIN_DIR}")
list (APPEND mirage_LDFLAGS ${GLIB_LDFLAGS})

to_list_spaces (mirage_CFLAGS mirage_CFLAGS_STR)
to_list_spaces (mirage_LDFLAGS mirage_LDFLAGS_STR)

set_target_properties (mirage PROPERTIES
    COMPILE_FLAGS ${mirage_CFLAGS_STR}
    COMPILE_DEFINITIONS "${mirage_DEFS}"
    LINK_FLAGS ${mirage_LDFLAGS_STR}
    LIBRARY_OUTPUT_NAME mirage
    VERSION ${MIRAGE_SOVERSION}
    SOVERSION ${MIRAGE_LT_CURRENT}
)

install (
    TARGETS mirage
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install (
    FILES ${libmirage_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libmirage
)

# *** File filters ***
foreach (filter ${filters_LIST})
    add_library (${filter} MODULE ${${filter}_SOURCES})
    target_link_libraries (${filter} ${GLIB_LIBRARIES})

    list (APPEND ${filter}_CFLAGS ${GLIB_CFLAGS})
    list (APPEND ${filter}_DEFS "")
    list (APPEND ${filter}_LDFLAGS ${GLIB_LDFLAGS})

    if (filter STREQUAL "filter-cso")
        list (APPEND ${filter}_CFLAGS ${ZLIB_CFLAGS})
        list (APPEND ${filter}_LDFLAGS ${ZLIB_LDFLAGS})
        target_link_libraries (${filter} ${ZLIB_LIBRARIES})
    elseif (filter STREQUAL "filter-daa")
        list (APPEND ${filter}_CFLAGS ${ZLIB_CFLAGS} "-I${PROJECT_SOURCE_DIR}/src/filters/filter-daa/lzma-sdk")
        list (APPEND ${filter}_LDFLAGS ${ZLIB_LDFLAGS})
        target_link_libraries (${filter} ${ZLIB_LIBRARIES})
    elseif (filter STREQUAL "filter-dmg")
        list (APPEND ${filter}_CFLAGS ${ZLIB_CFLAGS})
        list (APPEND ${filter}_LDFLAGS ${ZLIB_LDFLAGS})
        target_link_libraries (${filter} ${ZLIB_LIBRARIES} ${BZIP2_LIBRARY})
    elseif (filter STREQUAL "filter-gzip")
        list (APPEND ${filter}_CFLAGS ${ZLIB_CFLAGS})
        list (APPEND ${filter}_LDFLAGS ${ZLIB_LDFLAGS})
        target_link_libraries (${filter} ${ZLIB_LIBRARIES})
    elseif (filter STREQUAL "filter-isz")
        list (APPEND ${filter}_CFLAGS ${ZLIB_CFLAGS})
        list (APPEND ${filter}_LDFLAGS ${ZLIB_LDFLAGS})
        target_link_libraries (${filter} ${ZLIB_LIBRARIES} ${BZIP2_LIBRARY})
    elseif (filter STREQUAL "filter-sndfile")
        list (APPEND ${filter}_CFLAGS ${SNDFILE_CFLAGS} ${SAMPLERATE_CFLAGS})
        list (APPEND ${filter}_LDFLAGS ${SNDFILE_LDFLAGS} ${SAMPLERATE_LDFLAGS})
        target_link_libraries (${filter} ${SNDFILE_LIBRARIES} ${SAMPLERATE_LIBRARIES})
    elseif (filter STREQUAL "filter-xz")
        list (APPEND ${filter}_CFLAGS ${LIBLZMA_CFLAGS})
        list (APPEND ${filter}_LDFLAGS ${LIBLZMA_LDFLAGS})
        target_link_libraries (${filter} ${LIBLZMA_LIBRARIES})
    endif ()

    to_list_spaces (${filter}_CFLAGS ${filter}_CFLAGS_STR)
    to_list_spaces (${filter}_LDFLAGS ${filter}_LDFLAGS_STR)

    set_target_properties (${filter} PROPERTIES
        COMPILE_FLAGS "${${filter}_CFLAGS_STR}"
        COMPILE_DEFINITIONS "${${filter}_DEFS}"
        LINK_FLAGS "${${filter}_LDFLAGS_STR}"
        PREFIX ""
    )

    install (
        TARGETS ${filter}
        DESTINATION ${MIRAGE_PLUGIN_DIR}
    )
endforeach ()

# *** Parsers ***
foreach (parser ${parsers_LIST})
    add_library (${parser} MODULE ${${parser}_SOURCES})
    target_link_libraries (${parser} ${GLIB_LIBRARIES})

    list (APPEND ${parser}_CFLAGS ${GLIB_CFLAGS})
    list (APPEND ${parser}_DEFS "")
    list (APPEND ${parser}_LDFLAGS ${GLIB_LDFLAGS})

    to_list_spaces (${parser}_CFLAGS ${parser}_CFLAGS_STR)
    to_list_spaces (${parser}_LDFLAGS ${parser}_LDFLAGS_STR)

    set_target_properties (${parser} PROPERTIES
        COMPILE_FLAGS "${${parser}_CFLAGS_STR}"
        COMPILE_DEFINITIONS "${${parser}_DEFS}"
        LINK_FLAGS "${${parser}_LDFLAGS_STR}"
        PREFIX ""
    )

    install (
        TARGETS ${parser}
        DESTINATION ${MIRAGE_PLUGIN_DIR}
    )
endforeach ()

# *** Mimetype files ***
install (
    FILES ${mimetype_FILES}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
)
if (POST_INSTALL_HOOKS)
    install (CODE
        "execute_process (COMMAND ${UPDATE_MIME_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/mime)"
    )
endif ()

# *** Introspection ***
if (INTROSPECTION_ENABLED AND INTROSPECTION_FOUND)
    gobject_introspection (
        FILENAME libMirage-${MIRAGE_MAJOR_VERSION}.${MIRAGE_MINOR_VERSION}.gir
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

# *** Documentation ***
if (GTKDOC_ENABLED AND GTKDOC_FOUND)
    get_target_property (GTKDOC_LIB mirage LIBRARY_OUTPUT_NAME)
    gtk_doc (
        MODULE ${PROJECT_NAME}
        MAIN_SGML_FILE ${PROJECT_NAME}-docs.sgml
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/src/libmirage ${PROJECT_BINARY_DIR}/src/libmirage
        SOURCES ${libmirage_HEADERS} ${libmirage_SOURCES}
        DOCS_DIR ${PROJECT_SOURCE_DIR}/docs/reference/libmirage
        IGNORE_HFILES mirage-disc-structures.h
        CFLAGS ${mirage_CFLAGS} -I${PROJECT_SOURCE_DIR}/src/libmirage
        LDFLAGS ${mirage_LDFLAGS} -L${PROJECT_BINARY_DIR}
            -Wl,-rpath,${PROJECT_BINARY_DIR} -l${GTKDOC_LIB}
    )
endif ()

# *** Pkg-config ***
install (
    FILES ${PROJECT_BINARY_DIR}/libmirage.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

