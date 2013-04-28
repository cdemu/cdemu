set (filters_LIST
    filter-cso
    filter-daa
    filter-dmg
    filter-ecm
    filter-gzip
    filter-isz
    filter-macbinary
    filter-sndfile
    filter-xz
)

set (parsers_LIST
    image-b6t
    image-c2d
    image-ccd
    image-cdi
    image-cif
    image-cue
    image-harddisk
    image-iso
    image-mds
    image-mdx
    image-nrg
    image-readcd
    image-toc
    image-xcdroast
)

set (mimetype_FILES
    src/filters/filter-cso/libmirage-cso.xml
    src/filters/filter-daa/libmirage-daa.xml
    src/filters/filter-daa/libmirage-gbi.xml
    src/filters/filter-ecm/libmirage-ecm.xml
    src/filters/filter-isz/libmirage-isz.xml
    src/parsers/image-b6t/libmirage-b6t.xml
    src/parsers/image-c2d/libmirage-c2d.xml
    src/parsers/image-ccd/libmirage-ccd.xml
    src/parsers/image-cdi/libmirage-cdi.xml
    src/parsers/image-cif/libmirage-cif.xml
    src/parsers/image-harddisk/libmirage-cdr.xml
    src/parsers/image-mds/libmirage-mds.xml
    src/parsers/image-mds/libmirage-xmd.xml
    src/parsers/image-mdx/libmirage-mdx.xml
    src/parsers/image-nrg/libmirage-nrg.xml
    src/parsers/image-xcdroast/libmirage-xcdroast.xml
)

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

set (filter-xz_SOURCES
    src/filters/filter-xz/filter-xz-file-filter.c
    src/filters/filter-xz/filter-xz-plugin.c
)

set (filter-ecm_SOURCES
    src/filters/filter-ecm/filter-ecm-plugin.c
    src/filters/filter-ecm/filter-ecm-file-filter.c
)

set (filter-daa_SOURCES
    src/filters/filter-daa/lzma-sdk/Bra86.c
    src/filters/filter-daa/lzma-sdk/LzmaDec.c
    src/filters/filter-daa/filter-daa-file-filter.c
    src/filters/filter-daa/filter-daa-plugin.c
)

set (filter-dmg_SOURCES
    src/filters/filter-dmg/filter-dmg-file-filter.c
    src/filters/filter-dmg/filter-dmg-plugin.c
    src/filters/filter-dmg/resource-fork.c
    src/filters/filter-dmg/adc.c
)

set (filter-gzip_SOURCES
    src/filters/filter-gzip/filter-gzip-file-filter.c
    src/filters/filter-gzip/filter-gzip-plugin.c
)

set (filter-cso_SOURCES
    src/filters/filter-cso/filter-cso-plugin.c
    src/filters/filter-cso/filter-cso-file-filter.c
)

set (filter-isz_SOURCES
    src/filters/filter-isz/filter-isz-plugin.c
    src/filters/filter-isz/filter-isz-file-filter.c
)

set (filter-macbinary_SOURCES
    src/filters/filter-macbinary/filter-macbinary-plugin.c
    src/filters/filter-macbinary/filter-macbinary-file-filter.c
    src/filters/filter-dmg/resource-fork.c
)

set (filter-sndfile_SOURCES
    src/filters/filter-sndfile/filter-sndfile-plugin.c
    src/filters/filter-sndfile/filter-sndfile-file-filter.c
)

set (image-mdx_SOURCES
    src/parsers/image-mdx/image-mdx-plugin.c
    src/parsers/image-mdx/image-mdx-parser.c
)

set (image-cif_SOURCES
    src/parsers/image-cif/image-cif-parser.c
    src/parsers/image-cif/image-cif-plugin.c
)

set (image-toc_SOURCES
    src/parsers/image-toc/image-toc-plugin.c
    src/parsers/image-toc/image-toc-parser.c
)

set (image-readcd_SOURCES
    src/parsers/image-readcd/image-readcd-plugin.c
    src/parsers/image-readcd/image-readcd-parser.c
)

set (image-nrg_SOURCES
    src/parsers/image-nrg/image-nrg-parser.c
    src/parsers/image-nrg/image-nrg-plugin.c
)

set (image-iso_SOURCES
    src/parsers/image-iso/image-iso-parser.c
    src/parsers/image-iso/image-iso-plugin.c
)

set (image-harddisk_SOURCES
    src/parsers/image-harddisk/image-harddisk-parser.c
    src/parsers/image-harddisk/image-harddisk-plugin.c
)

set (image-cdi_SOURCES
    src/parsers/image-cdi/image-cdi-parser.c
    src/parsers/image-cdi/image-cdi-plugin.c
)

set (image-c2d_SOURCES
    src/parsers/image-c2d/image-c2d-parser.c
    src/parsers/image-c2d/image-c2d-plugin.c
)

set (image-b6t_SOURCES
    src/parsers/image-b6t/image-b6t-plugin.c
    src/parsers/image-b6t/image-b6t-parser.c
)

set (image-xcdroast_SOURCES
    src/parsers/image-xcdroast/image-xcdroast-parser.c
    src/parsers/image-xcdroast/image-xcdroast-plugin.c
)

set (image-cue_SOURCES
    src/parsers/image-cue/image-cue-parser.c
    src/parsers/image-cue/image-cue-plugin.c
)

set (image-ccd_SOURCES
    src/parsers/image-ccd/image-ccd-plugin.c
    src/parsers/image-ccd/image-ccd-parser.c
)

set (image-mds_SOURCES
    src/parsers/image-mds/image-mds-parser.c
    src/parsers/image-mds/image-mds-plugin.c
)

