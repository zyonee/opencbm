/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *  Copyright 1999-2001 Michael Klein <michael.klein@puffin.lb.shuttle.de>
 *  Modifications for cbm4win Copyright 2001-2004 Spiro Trikaliotis
*/

#ifdef SAVE_RCSID
static char *rcsid =
    "@(#) $Id: d64copy.c,v 1.2 2004-12-07 19:44:48 strik Exp $";
#endif

#include "d64copy_int.h"
#include <stdlib.h>
#include <string.h>

#include "arch.h"


static const char d64_sector_map[MAX_TRACKS+1] =
{ 0,
  21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
  21, 21, 21, 21, 21, 21, 21, 19, 19, 19,
  19, 19, 19, 19, 18, 18, 18, 18, 18, 18,
  17, 17, 17, 17, 17,
  17, 17, 17, 17, 17, 17, 17
};

static const char d71_sector_map[D71_TRACKS+1] =
{ 0,
  21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
  21, 21, 21, 21, 21, 21, 21, 19, 19, 19,
  19, 19, 19, 19, 18, 18, 18, 18, 18, 18,
  17, 17, 17, 17, 17,
  21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
  21, 21, 21, 21, 21, 21, 21, 19, 19, 19,
  19, 19, 19, 19, 18, 18, 18, 18, 18, 18,
  17, 17, 17, 17, 17,
};

static const unsigned char warp_read_1541[] =
{
#include "warpread1541.inc"
};

static const unsigned char warp_write_1541[] =
{
#include "warpwrite1541.inc"
};

static const unsigned char warp_read_1571[] =
{
#include "warpread1571.inc"
};

static const unsigned char warp_write_1571[] =
{
#include "warpwrite1571.inc"
};

static const unsigned char turbo_read_1541[] =
{
#include "turboread1541.inc"
};

static const unsigned char turbo_write_1541[] =
{
#include "turbowrite1541.inc"
};

static const unsigned char turbo_read_1571[] =
{
#include "turboread1571.inc"
};

static const unsigned char turbo_write_1571[] =
{
#include "turbowrite1571.inc"
};

static const struct drive_prog
{
    int size;
    const char *prog;
} drive_progs[] =
{
    {sizeof(turbo_read_1541), turbo_read_1541},
    {sizeof(turbo_write_1541), turbo_write_1541},
    {sizeof(warp_read_1541), warp_read_1541},
    {sizeof(warp_write_1541), warp_write_1541},
    {sizeof(turbo_read_1571), turbo_read_1571},
    {sizeof(turbo_write_1571), turbo_write_1571},
    {sizeof(warp_read_1571), warp_read_1571},
    {sizeof(warp_write_1571), warp_write_1571}
};


static const int default_interleave[] = { 17, 4, 13, 7 };
static const int warp_write_interleave[] = { 0, 6, 12, 4 };


static int send_turbo(CBM_FILE fd, unsigned char drv, int write, int warp, int drv_type)
{
    const struct drive_prog *prog;

    prog = &drive_progs[drv_type * 4 + warp * 2 + write];

    return cbm_upload(fd, drv, 0x500, prog->prog, prog->size);
}

extern transfer_funcs d64copy_fs_transfer,
                      d64copy_std_transfer,
                      d64copy_pp_transfer,
                      d64copy_s1_transfer,
                      d64copy_s2_transfer;

static d64copy_message_cb message_cb;
static d64copy_status_cb status_cb;

int d64copy_sector_count(int two_sided, int track)
{
    if(two_sided)
    {
        if(track >= 1 && track <= D71_TRACKS)
        {
            return d71_sector_map[track];
        }
    }
    else
    {
        if(track >= 1 && track <= TOT_TRACKS)
        {
            return d64_sector_map[track];
        }
    }
    return -1;
}

d64copy_settings *d64copy_get_default_settings(void)
{
    d64copy_settings *settings;

    settings = malloc(sizeof(d64copy_settings));

    if(NULL != settings)
    {
        settings->warp        = 0;
        settings->retries     = 0;
        settings->bam_mode    = bm_ignore;
        settings->interleave  = -1; /* set later on */
        settings->start_track = 1;
        settings->end_track   = -1; /* set later on */
        settings->drive_type  = cbm_dt_unknown; /* auto detect later on */
        settings->two_sided   = 0;
        settings->error_mode  = em_on_error;
    }
    return settings;
}


static int start_turbo(CBM_FILE fd, unsigned char drive)
{
    return cbm_exec_command(fd, drive, "U4:", 3);
}


static int copy_disk(CBM_FILE fd_cbm, d64copy_settings *settings,
              const transfer_funcs *src, const void *src_arg,
              const transfer_funcs *dst, const void *dst_arg, unsigned char cbm_drive)
{
    unsigned char tr = 0;
    unsigned char se = 0;
    int st;
    int cnt  = 0;
    unsigned char scnt = 0;
    unsigned char errors;
    int retry_count;
    int resend_trackmap;
    int max_tracks;
    char trackmap[MAX_SECTORS+1];
    char buf[40];
    unsigned const char *bam_ptr;
    unsigned char bam[BLOCKSIZE];
    unsigned char bam2[BLOCKSIZE];
    unsigned char block[BLOCKSIZE];
    unsigned char gcr[GCRBUFSIZE];
    const transfer_funcs *cbm_transf = NULL;
    d64copy_status status;
    const char *sector_map;
    const char *type_str = "*unknown*";

    if(settings->two_sided)
    {
        max_tracks = D71_TRACKS;
    }
    else
    {
        max_tracks = TOT_TRACKS;
    }

    if(settings->interleave != -1 &&
           (settings->interleave < 1 || settings->interleave > 17))
    {
        message_cb(0,
                "invalid value (%d) for interleave", settings->interleave);
        return -1;
    }

    if(settings->start_track < 1 || settings->start_track > max_tracks)
    {
        message_cb(0,
                "invalid value (%d) for start track", settings->start_track);
        return -1;
    }

    if(settings->end_track != -1 && 
       (settings->end_track < settings->start_track ||
        settings->end_track > max_tracks))
    {
        message_cb(0,
                "invalid value (%d) for end track", settings->end_track);
        return -1;
    }

    if(settings->interleave == -1)
    {
        settings->interleave = (dst->is_cbm_drive && settings->warp) ?
            warp_write_interleave[settings->transfer_mode] :
            default_interleave[settings->transfer_mode];
    }


    if(settings->drive_type == cbm_dt_unknown )
    {
        message_cb( 2, "Trying to identify drive type" );
        if( cbm_identify( fd_cbm, cbm_drive, &settings->drive_type, NULL ) )
        {
            message_cb( 0, "could not identify device" );
        }

        switch( settings->drive_type )
        {
            case cbm_dt_cbm1541:
            case cbm_dt_cbm1570:
            case cbm_dt_cbm1571:
                /* fine */
                break;
            case cbm_dt_cbm1581:
                message_cb( 0, "1581 drives are not supported" );
                return -1;
            default:
                message_cb( 1, "Unknown drive, assuming 1541" );
                settings->drive_type = cbm_dt_cbm1541;
                break;
        }
    }

    sector_map = settings->two_sided ? d71_sector_map : d64_sector_map;

    cbm_exec_command(fd_cbm, cbm_drive, "I0:", 0);
    cnt = cbm_device_status(fd_cbm, cbm_drive, buf, sizeof(buf));

    switch( settings->drive_type )
    {
        case cbm_dt_cbm1541: type_str = "1541"; break;
        case cbm_dt_cbm1570: type_str = "1570"; break;
        case cbm_dt_cbm1571: type_str = "1571"; break;
        default: /* impossible */ break;
    }

    message_cb(cnt != 0 ? 0 : 2, "drive %02d (%s): %s",
               cbm_drive, type_str, buf );

    if(cnt)
    {
        return -1;
    }

    if(settings->two_sided)
    {
        if(settings->drive_type != cbm_dt_cbm1571)
        {
            message_cb(0, ".d71 transfer requires a 1571 drive");
            return -1;
        }
        if(settings->warp)
        {
            message_cb(1, "`-w' for .d71 transfer in warp mode ignored");
            settings->warp = 0;
        }
        cbm_exec_command(fd_cbm, cbm_drive, "U0>M1", 0);
    }

    cbm_transf = src->is_cbm_drive ? src : dst;

    if(settings->warp && (cbm_transf->read_gcr_block == NULL))
    {
        message_cb(1, "`-w' for this transfer mode ignored");
        settings->warp = 0;
    }

    if(cbm_transf->needs_turbo)
    {
        send_turbo(fd_cbm, cbm_drive, dst->is_cbm_drive, settings->warp,
                   settings->drive_type == cbm_dt_cbm1541 ? 0 : 1);
    }

    if(src->open_disk(fd_cbm, settings, src_arg, 0,
                      start_turbo, message_cb) == 0)
    {
        if(settings->end_track == -1)
        {
            settings->end_track = 
                settings->two_sided ? D71_TRACKS : STD_TRACKS;
        }
        if(dst->open_disk(fd_cbm, settings, dst_arg, 1,
                          start_turbo, message_cb) != 0)
        {
            message_cb(0, "can't open destination");
            return -1;
        }
    }
    else
    {
        message_cb(0, "can't open source");
        return -1;
    }

    memset(status.bam, bs_invalid, MAX_TRACKS * MAX_SECTORS);

    if(settings->bam_mode != bm_ignore)
    {
        if(settings->warp && src->is_cbm_drive)
        {
            memset(trackmap, bs_dont_copy, sector_map[18]);
            trackmap[0] = bs_must_copy;
            scnt = 1;
            src->send_track_map(18, trackmap, scnt);
            st = src->read_gcr_block(&se, gcr);
            if(st == 0) st = gcr_decode(gcr, bam);
        }
        else
        {
            st = src->read_block(18, 0, bam);
            if(settings->two_sided && (st == 0))
            {
                st = src->read_block(53, 0, bam2);
            }
        }
        if(st)
        {
            message_cb(1, "failed to read BAM (%d)", st);
            settings->bam_mode = bm_ignore;
        }
    }

    memset(&status, 0, sizeof(status));

    /* setup BAM */
    for(tr = 1; tr <= max_tracks; tr++)
    {
        if(tr < settings->start_track || tr > settings->end_track)
        {
            memset(status.bam[tr-1], bs_dont_copy, sector_map[tr]);
        }
        else if(settings->bam_mode == bm_allocated ||
                (settings->bam_mode == bm_save && (tr % 35 != 18)))
        {
            for(se = 0; se < sector_map[tr]; se++)
            {
                if(settings->two_sided && tr > STD_TRACKS)
                {
                    bam_ptr = &bam2[3*(tr - STD_TRACKS - 1)];
                }
                else
                {
                    bam_ptr = &bam[4*tr + 1 + (tr > STD_TRACKS ? 48 : 0)];
                }
                if(bam_ptr[se/8]&(1<<(se&0x07)))
                {
                    status.bam[tr-1][se] = bs_dont_copy;
                }
                else
                {
                    status.bam[tr-1][se] = bs_must_copy;
                    status.total_sectors++;
                }
            }
        }
        else
        {
            status.total_sectors += sector_map[tr];
            memset(status.bam[tr-1], bs_must_copy, sector_map[tr]);
        }
    }

    status.settings = settings;

    status_cb(status);

    message_cb(2, "copying tracks %d-%d (%d sectors)",
            settings->start_track, settings->end_track, status.total_sectors);

    for(tr = 1; tr <= max_tracks; tr++)
    {
        if(tr >= settings->start_track && tr <= settings->end_track)
        {
            scnt = sector_map[tr];
            memcpy(trackmap, status.bam[tr-1], scnt);
            if(settings->bam_mode != bm_ignore)
            {
                for(se = 0; se < sector_map[tr]; se++)
                {
                    if(trackmap[se] != bs_must_copy)
                    {
                        scnt--;
                    }
                }
            }

            retry_count = settings->retries;
            do
            {
                errors = resend_trackmap = 0;
                if(scnt && settings->warp && src->is_cbm_drive)
                {
                    src->send_track_map(tr, trackmap, scnt);
                }
                else
                {
                    se = 0;
                }
                while(scnt && !resend_trackmap)
                {
                    if(settings->warp && src->is_cbm_drive)
                    {
                        status.read_result = src->read_gcr_block(&se, gcr);
                        if(status.read_result == 0)
                        {
                            status.read_result = gcr_decode(gcr, block);
                        }
                        else
                        {
                            /* mark all sectors not received so far */
                            /* ugly */
                            errors = 0;
                            for(scnt = 0; scnt < sector_map[tr]; scnt++)
                            {
                                if(NEED_SECTOR(trackmap[scnt]) && scnt != se)
                                {
                                    trackmap[scnt] = bs_error;
                                    errors++;
                                }
                            }
                            resend_trackmap = 1;
                        }
                    }
                    else
                    {
                        while(!NEED_SECTOR(trackmap[se]))
                        {
                            if(++se >= sector_map[tr]) se = 0;
                        }
                        status.read_result = src->read_block(tr, se, block);
                    }
                    if(status.read_result && status.read_result < 20)
                    {
                        /* map job code to DOS code */
                        status.read_result += 18;
                    }

                    if(settings->warp && dst->is_cbm_drive)
                    {
                        gcr_encode(block, gcr);
                        status.write_result = 
                            dst->write_block(tr, se, gcr, GCRBUFSIZE-1,
                                             status.read_result);
                    }
                    else
                    {
                        status.write_result = 
                            dst->write_block(tr, se, block, BLOCKSIZE,
                                             status.read_result);
                    }

                    if(status.read_result)
                    {
                        /* read error */
                        trackmap[se] = bs_error;
                        errors++;
                        if(retry_count == 0)
                        {
                            status.sectors_processed++;
                            /* FIXME: shall we get rid of this? */
                            message_cb( 1, "read error: %02x/%02x: %d",
                                        tr, se, status.read_result );
                        }
                    }
                    else
                    {
                        /* successfull read */
                        if(status.write_result)
                        {
                            /* write error */
                            trackmap[se] = bs_error;
                            errors++;
                            if(retry_count == 0)
                            {
                                status.sectors_processed++;
                                /* FIXME: shall we get rid of this? */
                                message_cb(1, "write error: %02x/%02x: %d",
                                           tr, se, status.write_result);
                            }
                        }
                        else
                        {
                            /* successfull read and write, mark sector */
                            trackmap[se] = bs_copied;
                            cnt++;
                            status.sectors_processed++;
                        }
                    }
                    /* remaining sectors on this track */
                    if(!resend_trackmap)
                    {
                        scnt--;
                    }

                    status.track = tr;
                    status.sector= se;

                    status_cb(status);

                    if(dst->is_cbm_drive || !settings->warp)
                    {
                        se += (unsigned char) settings->interleave;
                        if(se >= sector_map[tr]) se -= sector_map[tr];
                    }
                }
                if(errors > 0 && settings->retries >= 0)
                {
                    retry_count--;
                    scnt = errors;
                }
            }
            while(retry_count >= 0 && errors > 0);
            if(errors)
            {
                message_cb(1, "giving up...");
            }
        }
        if(settings->two_sided)
        {
            if(tr <= STD_TRACKS)
            {
                if(tr + STD_TRACKS <= D71_TRACKS)
                {
                    tr += (STD_TRACKS - 1);
                }
            }
            else if(tr != D71_TRACKS)
            {
                tr -= STD_TRACKS;
            }
        }
    }

    dst->close_disk();
    src->close_disk();

    return cnt;
}


static struct _transfers
{
    const transfer_funcs *trf;
    const char *name, *abbrev;
}
transfers[] =
{
    { &d64copy_std_transfer, "original", "o%" },
    { &d64copy_s1_transfer, "serial1", "s1" },
    { &d64copy_s2_transfer, "serial2", "s2" },
    { &d64copy_pp_transfer, "parallel", "p%" },
    { NULL, NULL, NULL }
};


char *d64copy_get_transfer_modes()
{
    const struct _transfers *t;
    int size;
    char *buf;
    char *dst;

    size = 1; /* for terminating '\0' */
    for(t = transfers; t->trf; t++)
    {
        size += (strlen(t->name) + 1);
    }

    buf = malloc(size);

    if(buf)
    {
        dst = buf;
        for(t = transfers; t->trf; t++)
        {
            strcpy(dst, t->name);
            dst += (strlen(t->name) + 1);
        }
        *dst = '\0';
    }

    return buf;
}


int d64copy_get_transfer_mode_index(const char *name)
{
    const struct _transfers *t;
    int i;
    int abbrev_len;
    int tm_len;

    if(NULL == name)
    {
        /* default transfer mode */
        return 0;
    }

    tm_len = strlen(name);
    for(i = 0, t = transfers; t->trf; i++, t++)
    {
        if(arch_strcasecmp(name, t->name) == 0)
        {
            /* full match */
            return i;
        }
        if(t->abbrev[strlen(t->abbrev)-1] == '%')
        {
            abbrev_len = strlen(t->abbrev) - 1;
            if(abbrev_len <= tm_len && arch_strncasecmp(t->name, name, tm_len) == 0)
            {
                return i;
            }
        }
        else
        {
            if(strcmp(name, t->abbrev) == 0)
            {
                return i;
            }
        }
    }
    return -1;
}


int d64copy_read_image(CBM_FILE cbm_fd,
                       d64copy_settings *settings,
                       int src_drive,
                       const char *dst_image,
                       d64copy_message_cb msg_cb,
                       d64copy_status_cb stat_cb)
{
    const transfer_funcs *src;
    const transfer_funcs *dst;

    message_cb = msg_cb;
    status_cb = stat_cb;

    src = transfers[settings->transfer_mode].trf;
    dst = &d64copy_fs_transfer;

    return copy_disk(cbm_fd, settings,
            src, (void*)src_drive, dst, (void*)dst_image, (unsigned char) src_drive);
}

int d64copy_write_image(CBM_FILE cbm_fd,
                        d64copy_settings *settings,
                        const char *src_image,
                        int dst_drive,
                        d64copy_message_cb msg_cb,
                        d64copy_status_cb stat_cb)
{
    const transfer_funcs *src;
    const transfer_funcs *dst;

    message_cb = msg_cb;
    status_cb = stat_cb;

    src = &d64copy_fs_transfer;
    dst = transfers[settings->transfer_mode].trf;

    return copy_disk(cbm_fd, settings,
            src, (void*)src_image, dst, (void*)dst_drive, (unsigned char) dst_drive);
}