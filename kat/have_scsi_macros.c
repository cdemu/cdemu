#include <linux/autoconf.h>
#include <scsi/scsi_cmnd.h>

void testfunc () {
    struct scsi_cmnd cmd;
    
    /* These have appeared in 2.6.23 */
    scsi_set_resid(&cmd, 0);
    scsi_get_resid(&cmd);
    scsi_sg_count(&cmd);
    scsi_sglist(&cmd);
    scsi_bufflen(&cmd);
}

//T#define KAT_HAVE_SCSI_MACROS
//F
