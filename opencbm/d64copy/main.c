/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *  Copyright 1999-2001 Michael Klein <michael.klein@puffin.lb.shuttle.de>
*/

#ifdef SAVE_RCSID
static char *rcsid =
    "@(#) $Id: main.c,v 1.3 2004-12-07 19:44:45 strik Exp $";
#endif

#include "opencbm.h"
#include "d64copy.h"

#include "arch.h"

#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>


/* setable via command line */
static int verbosity = 1;
static int no_progress = 0;

/* other globals */
static CBM_FILE fd_cbm;


static int is_cbm(char *name)
{
    return((strcmp(name, "8" ) == 0) || (strcmp(name, "9" ) == 0) ||
           (strcmp(name, "10") == 0) || (strcmp(name, "11") == 0) );
}


static void help()
{
    printf(
"Usage: d64copy [OPTION]... [SOURCE] [TARGET]\n"
"Copy .d64 disk images to a CBM-1541 or compatible drive and vice versa\n"
"\n"
"Options:\n"
"  -h, --help               display this help and exit\n"
"  -V, --version            display version information and exit\n"
"  -q, --quiet              quiet output\n"
"  -v, --verbose            control verbosity (repeatedly, up to 3 times)\n"
"  -n, --no-progress        do not display progress information\n"
"\n"
"  -s, --start-track=TRACK  set start track\n"
"  -e, --end-track=TRACK    set end track (start <= end <= 42/70)\n"
"\n"
"  -t, --transfer=TRANSFER  set transfermode; valid modes:\n"
"                             original       (slowest)\n"
"                             serial1 or s1\n"
"                             serial2 or s2\n"
"                             parallel       (fastest)\n"
"                           (can be abbreviated, if unambiguous)\n"
"                           `original' and `serial1' should work in any case;\n"
"                           `serial2' won't work if more than one device is\n"
"                           connected to the IEC bus;\n"
"                           `parallel' needs a XP1541/XP1571 cable in addition"
"                           to the serial one.\n"
"\n"
"  -i, --interleave=VALUE   set interleave value; ignored when reading with\n"
"                           warp mode; default values are:\n"
"\n"
"                             original     16\n"
"\n"
"                                       turbo r/w   warp write\n"
"                             serial1       4            6\n"
"                             serial2      13           12\n"
"                             parallel      7            4\n"
"\n"
"                           INTERLEAVE is ignored when reading with warp mode;\n"
"                           if data transfer is very slow, increasing this\n"
"                           value may help.\n"
"\n"
"  -w, --warp               enable warp mode; this is not possible when\n"
"                           TRANSFER is set to `original'\n"
"\n"
"  -b, --bam-only           BAM-only copy; only allocated blocks are copied;\n"
"                           for extended tracks (36-40), SpeedDOS BAM format\n"
"                           is assumed. Use with caution.\n"
"\n"
"  -B, --bam-save           save BAM-only copy; this is like the `-b� option\n"
"                           but copies always the entire directory track.\n"
"\n"
"  -d, --drive-type=TYPE    specify drive type:\n"
"                             0 or 1541 = 1541\n"
"                             1 or 1571 = 1570/1571\n"
"\n"
"  -r, --retry-count=COUNT  set retry count\n"
"\n"
"  -E, --error-map=WHEN     control whether the error map is appended.\n"
"                           possible values for WHEN are (abbreviations\n"
"                           available):\n"
"                             always\n"
"                             on_errors     (default)\n"
"                             never\n"
"\n"
"  -2, --two-sided          two-sided disk transfer (.d71): Requires 1571.\n"
"                           Warp mode is not available for .d71 images.\n"
"\n"
);
}

static void hint(char *s)
{
    fprintf(stderr, "Try `%s' --help for more information.\n", s);
}

static void ARCH_SIGNALDECL reset(int dummy)
{
    fprintf(stderr, "\nSIGINT caught X-(  Resetting IEC bus...\n");
    arch_sleep(1);
    cbm_reset(fd_cbm);
    cbm_driver_close(fd_cbm);
    exit(1);
}


static void my_message_cb(int severity, const char *format, ...)
{
    va_list args;

    static const char *severities[4] =
    {
        "Fatal",
        "Warning",
        "Info",
        "Debug"
    };

    if(verbosity >= severity)
    {
        fprintf(stderr, "[%s] ", severities[severity]);
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        fprintf(stderr, "\n");
    }
}

static int my_status_cb(d64copy_status status)
{
    static char trackmap[MAX_SECTORS+1];
    static int last_track;
    char *s;
    char *d;

    static const char bs2char[] =
    {
        ' ', '.', '-', '?', '*'
    };

    if(status.track == 0)
    {
        last_track = 0;
        return 0;
    }

    if(no_progress)
    {
        return 0;
    }

    if(last_track != status.track)
    {
        if(last_track)
        {
            printf("\r%2d: %-24s               \n", last_track, trackmap);
        }

        for(s = status.bam[status.track-1], d = trackmap; *s; s++, d++)
        {
            *d = bs2char[(int)*s];
        }
        *d = '\0';
        last_track = status.track;
    }

    trackmap[status.sector] = 
        bs2char[(status.read_result || 
                 status.write_result) ? bs_error : bs_copied];

    printf("\r%2d: %-24s%3d%%  %4d/%d", status.track, trackmap,
           100 * status.sectors_processed / status.total_sectors,
           status.sectors_processed, status.total_sectors);

    fflush(stdout);
    return 0;
}


int ARCH_MAINDECL main(int argc, char *argv[])
{
    d64copy_settings *settings;

    char *tm = NULL;
    char *src_arg;
    char *dst_arg;

    int  c;
    int  rv = 1;
    int  l;

    int src_is_cbm;
    int dst_is_cbm;

    struct option longopts[] =
    {
        { "help"       , no_argument      , NULL, 'h' },
        { "version"    , no_argument      , NULL, 'V' },
        { "warp"       , no_argument      , NULL, 'w' },
        { "quiet"      , no_argument      , NULL, 'q' },
        { "verbose"    , no_argument      , NULL, 'v' },
        { "no-progress", no_argument      , NULL, 'n' },
        { "interleave" , required_argument, NULL, 'i' },
        { "start-track", required_argument, NULL, 's' },
        { "end-track"  , required_argument, NULL, 'e' },
        { "transfer"   , required_argument, NULL, 't' },
        { "bam-only"   , no_argument      , NULL, 'b' },
        { "bam-save"   , no_argument      , NULL, 'B' },
        { "drive-type" , required_argument, NULL, 'd' },
        { "retry-count", required_argument, NULL, 'r' },
        { "two-sided"  , no_argument      , NULL, '2' },
        { "error-map"  , required_argument, NULL, 'E' },
        { NULL         , 0                , NULL, 0   }
    };

    const char shortopts[] ="hVwqbBt:i:s:e:d:r:2vnE:";

    settings = d64copy_get_default_settings();

    while((c=getopt_long(argc, argv, shortopts, longopts, NULL)) != -1)
    {
        switch(c)
        {
            case 'h': help();
                      return 0;
            case 'V': printf("d64copy %s\n", VERSION);
                      return 0;
            case 'w': settings->warp = 1;
                      break;
            case 'q': if(verbosity > 0) verbosity--;
                      break;
            case 'v': verbosity++;
                      break;
            case 'n': no_progress = 1;
                      break;
            case 'i': settings->interleave = arch_atoc(optarg);
                      break;
            case 's': settings->start_track = atoi(optarg);
                      break;
            case 'e': settings->end_track = atoi(optarg);
                      break;
            case 't': tm = optarg;
                      break;
            case 'b': settings->bam_mode = bm_allocated;
                      break;
            case 'B': settings->bam_mode = bm_save;
                      break;
            case 'd': if(strcmp(optarg, "1541") == 0)
                      {
                          settings->drive_type = cbm_dt_cbm1541;
                      }
                      else if(strcmp(optarg, "1571") == 0)
                      {
                          settings->drive_type = cbm_dt_cbm1571;
                      }
                      else if(strcmp(optarg, "1570") == 0)
                      {
                          settings->drive_type = cbm_dt_cbm1570;
                      }
                      else
                      {
                          settings->drive_type = atoi(optarg) != 0 ?
                              cbm_dt_cbm1571 : cbm_dt_cbm1541;
                      }
                      break;
            case 'r': settings->retries = atoi(optarg);
                      break;
            case '2': settings->two_sided = 1;
                      break;
            case 'E': l = strlen(optarg);
                      if(strncmp(optarg, "always", l) == 0)
                      {
                          settings->error_mode = em_always;
                      }
                      else if(strncmp(optarg, "on_errors", l) == 0)
                      {
                          settings->error_mode = em_on_error;
                      }
                      else if(strncmp(optarg, "never", l) == 0)
                      {
                          settings->error_mode = em_never;
                      }
                      else
                      {
                          hint(argv[0]);
                          return 1;
                      }
                      break;
            default : hint(argv[0]);
                      return 1;
        }
    }

    settings->transfer_mode = d64copy_get_transfer_mode_index(tm);
    if(settings->transfer_mode < 0)
    {
        char *modes = d64copy_get_transfer_modes();
        char *m;

        fprintf(stderr, "Unknown transfer mode: %s\nAvailable modes:\n", tm);

        for(m = modes; *m; m+=(strlen(m)+1))
        {
            fprintf(stderr, "  %s\n", m);
        }

        free(modes);
        return 1;
    }

    my_message_cb(3, "transfer mode is %d", settings->transfer_mode );

    if(optind + 2 != argc)
    {
        fprintf(stderr, "Usage: %s [OPTION]... [SOURCE] [TARGET]\n", argv[0]);
        hint(argv[0]);
        return 1;
    }

    src_arg = argv[optind];
    dst_arg = argv[optind+1];

    src_is_cbm = is_cbm(src_arg);
    dst_is_cbm = is_cbm(dst_arg);

    if(src_is_cbm == dst_is_cbm)
    {
        my_message_cb(0, "either source or target must be a CBM drive");
        return 1;
    }

    if(cbm_driver_open(&fd_cbm, 0) == 0)
    {
        signal(SIGINT, reset);

        if(src_is_cbm)
        {
            rv = d64copy_read_image(fd_cbm, settings, atoi(src_arg), dst_arg,
                    my_message_cb, my_status_cb);
        }
        else
        {
            rv = d64copy_write_image(fd_cbm, settings, src_arg, atoi(dst_arg),
                    my_message_cb, my_status_cb);
        }

        if(!no_progress && rv >= 0)
        {
            printf("\n%d blocks copied.\n", rv);
        }

        cbm_driver_close(fd_cbm);
        rv = 0;
    }
    else
    {
        arch_error(0, arch_get_errno(), "%s", cbm_get_driver_name(0));
    }

    free(settings);
    
    return rv;
}