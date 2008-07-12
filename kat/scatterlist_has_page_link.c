#include <linux/autoconf.h>
#include <linux/scatterlist.h>

/* Note: scatterlist.page_link is used in kernel (2.6.24 <= version < X) */

struct scatterlist s = { .page_link = 0 };

//T#define KAT_SCATTERLIST_HAS_PAGE_LINK
//F#include <linux/scatterlist.h>

