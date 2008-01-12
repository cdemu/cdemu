#include <linux/autoconf.h>
#include <linux/scatterlist.h>

struct scatterlist s = { .page = 0 };

//T#define KAT_SCATTERLIST_HAS_PAGE
//F#include <linux/scatterlist.h>
