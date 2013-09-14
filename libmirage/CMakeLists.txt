cmake_minimum_required (VERSION 2.8.5)

# Project name
project (libmirage C)

# Release versioning:
set (MIRAGE_MAJOR_VERSION 2)
set (MIRAGE_MINOR_VERSION 1)
set (MIRAGE_MICRO_VERSION 1)
set (MIRAGE_VERSION_LONG ${MIRAGE_MAJOR_VERSION}.${MIRAGE_MINOR_VERSION}.${MIRAGE_MICRO_VERSION})
set (MIRAGE_VERSION_SHORT ${MIRAGE_MAJOR_VERSION}.${MIRAGE_MINOR_VERSION})

# Libtool versioning:
# Increment if the interface has additions, changes, removals.
set (MIRAGE_LT_CURRENT 9)
# Increment any time the source changes; set to 0 if you increment CURRENT
set (MIRAGE_LT_REVISION 0)
# Increment if any interfaces have been added; set to 0 if any interfaces have
# been removed. Removal has precedence over adding, so set to 0 if both happened.
set (MIRAGE_LT_AGE 2)
set (MIRAGE_SOVERSION ${MIRAGE_LT_CURRENT}.${MIRAGE_LT_REVISION}.${MIRAGE_LT_AGE})

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (GNUInstallDirs)

# Options
option (GTKDOC_ENABLED "Use gtk-doc to build documentation" on)
option (INTROSPECTION_ENABLED "Generate gobject introspection files" on)
option (POST_INSTALL_HOOKS "Run post-install hooks" on)

# Plugin directory
set (MIRAGE_PLUGIN_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/libmirage-${MIRAGE_VERSION_SHORT}" CACHE PATH "Path to libMirage plugin directory." FORCE)

# Dependencies
find_package (PkgConfig 0.16 REQUIRED)

pkg_check_modules (GLIB REQUIRED glib-2.0>=2.24 gobject-2.0>=2.24 gmodule-2.0>=2.24 gio-2.0>=2.24)

if (INTROSPECTION_ENABLED)
    pkg_check_modules (INTROSPECTION gobject-introspection-1.0>=1.30.0)
    if (INTROSPECTION_FOUND)
        include (GObjectIntrospection)
        set (INTROSPECTION_STATUS "ON")
    else ()
        set (INTROSPECTION_STATUS "OFF (not found)")
    endif ()
else ()
    set (INTROSPECTION_STATUS "OFF (disabled)")
endif ()

if (GTKDOC_ENABLED)
    pkg_check_modules (GTKDOC gtk-doc>=1.4)
    if (GTKDOC_FOUND)
        include (GtkDoc)
        set (GTKDOC_STATUS "ON")
    else ()
        set (GTKDOC_STATUS "OFF (not found)")
    endif ()
else ()
    set (GTKDOC_STATUS "OFF (disabled)")
endif ()

if (POST_INSTALL_HOOKS)
    find_program(UPDATE_MIME_DATABASE_EXECUTABLE NAMES update-mime-database)
    mark_as_advanced(UPDATE_MIME_DATABASE_EXECUTABLE)
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

include_directories (${GLIB_INCLUDE_DIRS})

# Link directories
link_directories (${GLIB_LIBRARY_DIRS})

# *** libMirage core library ***
set (libmirage_HEADERS
    src/libmirage/mirage.h
    src/libmirage/mirage-cdtext-coder.h
    src/libmirage/mirage-context.h
    src/libmirage/mirage-contextual.h
    src/libmirage/mirage-debug.h
    src/libmirage/mirage-disc.h
    src/libmirage/mirage-disc-structures.h
    src/libmirage/mirage-error.h
    src/libmirage/mirage-file-filter.h
    src/libmirage/mirage-fragment.h
    src/libmirage/mirage-index.h
    src/libmirage/mirage-language.h
    src/libmirage/mirage-object.h
    src/libmirage/mirage-parser.h
    src/libmirage/mirage-plugin.h
    src/libmirage/mirage-sector.h
    src/libmirage/mirage-session.h
    src/libmirage/mirage-track.h
    src/libmirage/mirage-types.h
    src/libmirage/mirage-utils.h
    ${PROJECT_BINARY_DIR}/src/libmirage/mirage-version.h
)

set (libmirage_SOURCES
    src/libmirage/mirage.c
    src/libmirage/mirage-cdtext-coder.c
    src/libmirage/mirage-context.c
    src/libmirage/mirage-contextual.c
    src/libmirage/mirage-disc.c
    src/libmirage/mirage-error.c
    src/libmirage/mirage-file-filter.c
    src/libmirage/mirage-fragment.c
    src/libmirage/mirage-index.c
    src/libmirage/mirage-language.c
    src/libmirage/mirage-object.c
    src/libmirage/mirage-parser.c
    src/libmirage/mirage-plugin.c
    src/libmirage/mirage-sector.c
    src/libmirage/mirage-session.c
    src/libmirage/mirage-track.c
    src/libmirage/mirage-utils.c
    src/libmirage/mirage-version.c
)

add_library (mirage SHARED ${libmirage_SOURCES})
target_link_libraries (mirage ${GLIB_LIBRARIES})

set_target_properties (mirage PROPERTIES
    LIBRARY_OUTPUT_NAME mirage
    VERSION ${MIRAGE_SOVERSION}
    SOVERSION ${MIRAGE_LT_CURRENT}
)

install (TARGETS mirage DESTINATION ${CMAKE_INSTALL_LIBDIR})
install (FILES ${libmirage_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libmirage)

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
        CFLAGS ${GLIB_CFLAGS} -I${PROJECT_SOURCE_DIR}/src/libmirage
        LDFLAGS ${GLIB_LDFLAGS} -L${PROJECT_BINARY_DIR}
            -Wl,-rpath,${PROJECT_BINARY_DIR} -l${GTKDOC_LIB}
    )
endif ()

# *** Pkg-config ***
install (
    FILES ${PROJECT_BINARY_DIR}/libmirage.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# *** File filters ***
add_subdirectory(src/filters/filter-cso)
add_subdirectory(src/filters/filter-daa)
add_subdirectory(src/filters/filter-dmg)
add_subdirectory(src/filters/filter-ecm)
add_subdirectory(src/filters/filter-gzip)
add_subdirectory(src/filters/filter-isz)
add_subdirectory(src/filters/filter-macbinary)
add_subdirectory(src/filters/filter-sndfile)
add_subdirectory(src/filters/filter-xz)

# *** Image parsers ***
add_subdirectory(src/parsers/image-b6t)
add_subdirectory(src/parsers/image-c2d)
add_subdirectory(src/parsers/image-ccd)
add_subdirectory(src/parsers/image-cdi)
add_subdirectory(src/parsers/image-cif)
add_subdirectory(src/parsers/image-cue)
add_subdirectory(src/parsers/image-harddisk)
add_subdirectory(src/parsers/image-iso)
add_subdirectory(src/parsers/image-mds)
add_subdirectory(src/parsers/image-mdx)
add_subdirectory(src/parsers/image-nrg)
add_subdirectory(src/parsers/image-readcd)
add_subdirectory(src/parsers/image-toc)
add_subdirectory(src/parsers/image-xcdroast)

# *** Configuration summary ***
message(STATUS "")
message(STATUS "*** libMirage v.${MIRAGE_VERSION_LONG} configuration summary ***")
message(STATUS "Compiler and build info:")
message(STATUS " C compiler: " ${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1})
message(STATUS " C flags (Release): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE})
message(STATUS " C flags (Debug): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG})
message(STATUS " build type: " ${CMAKE_BUILD_TYPE})
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
message(STATUS " plugin dir: " ${MIRAGE_PLUGIN_DIR})
message(STATUS "File filters:")
message(STATUS " enabled: " ${FILTERS_ENABLED})
message(STATUS " disabled: " ${FILTERS_DISABLED})
message(STATUS "Image parsers:")
message(STATUS " enabled: " ${PARSERS_ENABLED})
message(STATUS " disabled: " ${PARSERS_DISABLED})
message(STATUS "Options:")
message(STATUS " build gtk-doc documentation: " ${GTKDOC_STATUS})
message(STATUS " build gobject-introspection bindings: " ${INTROSPECTION_STATUS})
message(STATUS " run post-install hooks: " ${POST_INSTALL_HOOKS})
message(STATUS "")
