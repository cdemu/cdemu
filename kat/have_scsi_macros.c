#include <scsi/scsi_cmnd.h>

void testfunc (void);
void testfunc (void) {
    struct scsi_cmnd cmd;
    
    /* These have appeared in 2.6.23 */
    scsi_set_resid(&cmd, 0);
    scsi_get_resid(&cmd);
    scsi_sg_count(&cmd);
    scsi_sglist(&cmd);
    scsi_bufflen(&cmd);
}

//  If current kernel is soo old - inject macros
//F#define scsi_sg_count(cmd) ((cmd)->use_sg)
//F#define scsi_sglist(cmd) ((cmd)->request_buffer)
//F#define scsi_bufflen(cmd) ((cmd)->request_bufflen)
//F#define scsi_set_resid(cmd, to_read) {(cmd)->resid = (to_read);}
