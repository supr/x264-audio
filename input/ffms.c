/*****************************************************************************
 * ffms.c: ffmpegsource input
 *****************************************************************************
 * Copyright (C) 2009-2011 x264 project
 *
 * Authors: Mike Gurlitz <mike.gurlitz@gmail.com>
 *          Steven Walters <kemuri9@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at licensing@x264.com.
 *****************************************************************************/

#include "input.h"
#include <ffms.h>
#define FAIL_IF_ERROR( cond, ... ) FAIL_IF_ERR( cond, "ffms", __VA_ARGS__ )

#undef DECLARE_ALIGNED
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#ifdef _WIN32
#include <windows.h>
#else
#define SetConsoleTitle(t)
#endif

#if HAVE_AUDIO
#include "audio/audio.h"
#endif

typedef struct
{
    FFMS_VideoSource *video_source;
    FFMS_Track *track;
    int reduce_pts;
    int vfr_input;
    int num_frames;
    int64_t time;
#if HAVE_AUDIO
    char *filename;
    int has_audio;
#endif
} ffms_hnd_t;

static int FFMS_CC update_progress( int64_t current, int64_t total, void *private )
{
    int64_t *update_time = private;
    int64_t oldtime = *update_time;
    int64_t newtime = x264_mdate();
    if( oldtime && newtime - oldtime < UPDATE_INTERVAL )
        return 0;
    *update_time = newtime;

    char buf[200];
    sprintf( buf, "ffms [info]: indexing input file [%.1f%%]", 100.0 * current / total );
    fprintf( stderr, "%s  \r", buf+5 );
    SetConsoleTitle( buf );
    fflush( stderr );
    return 0;
}

static int open_file( char *psz_filename, hnd_t *p_handle, video_info_t *info, cli_input_opt_t *opt )
{
    ffms_hnd_t *h = calloc( 1, sizeof(ffms_hnd_t) );
    if( !h )
        return -1;
    FFMS_Init( 0, 0 );
    FFMS_ErrorInfo e;
    e.BufferSize = 0;
    int seekmode = opt->seek ? FFMS_SEEK_NORMAL : FFMS_SEEK_LINEAR_NO_RW;

    FFMS_Index *idx = NULL;
    if( opt->index_file )
    {
        struct stat index_s, input_s;
        if( !stat( opt->index_file, &index_s ) && !stat( psz_filename, &input_s ) &&
            input_s.st_mtime < index_s.st_mtime )
            idx = FFMS_ReadIndex( opt->index_file, &e );
    }
    if( !idx )
    {
        if( opt->progress )
        {
            idx = FFMS_MakeIndex( psz_filename, 0, 0, NULL, NULL, 0, update_progress, &h->time, &e );
            fprintf( stderr, "                                            \r" );
        }
        else
            idx = FFMS_MakeIndex( psz_filename, 0, 0, NULL, NULL, 0, NULL, NULL, &e );
        FAIL_IF_ERROR( !idx, "could not create index\n" )
        if( opt->index_file && FFMS_WriteIndex( opt->index_file, idx, &e ) )
            x264_cli_log( "ffms", X264_LOG_WARNING, "could not write index file\n" );
    }

    int trackno = FFMS_GetFirstTrackOfType( idx, FFMS_TYPE_VIDEO, &e );
    FAIL_IF_ERROR( trackno < 0, "could not find video track\n" )

#if HAVE_AUDIO
    h->filename  = strdup( psz_filename );
    h->has_audio = !!( FFMS_GetFirstTrackOfType( idx, FFMS_TYPE_AUDIO, &e ) > 0 );
#endif

    h->video_source = FFMS_CreateVideoSource( psz_filename, trackno, idx, 1, seekmode, &e );
    FAIL_IF_ERROR( !h->video_source, "could not create video source\n" )

    h->track = FFMS_GetTrackFromVideo( h->video_source );

    FFMS_DestroyIndex( idx );
    const FFMS_VideoProperties *videop = FFMS_GetVideoProperties( h->video_source );
    info->num_frames   = h->num_frames = videop->NumFrames;
    info->sar_height   = videop->SARDen;
    info->sar_width    = videop->SARNum;
    info->fps_den      = videop->FPSDenominator;
    info->fps_num      = videop->FPSNumerator;
    h->vfr_input       = info->vfr;
    /* ffms is thread unsafe as it uses a single frame buffer for all frame requests */
    info->thread_safe  = 0;

    const FFMS_Frame *frame = FFMS_GetFrame( h->video_source, 0, &e );
    FAIL_IF_ERROR( !frame, "could not read frame 0\n" )

    info->width      = frame->EncodedWidth;
    info->height     = frame->EncodedHeight;
    info->csp        = frame->EncodedPixelFormat | X264_CSP_OTHER;
    info->interlaced = frame->InterlacedFrame;
    info->tff        = frame->TopFieldFirst;

    /* ffms timestamps are in milliseconds. ffms also uses int64_ts for timebase,
     * so we need to reduce large timebases to prevent overflow */
    if( h->vfr_input )
    {
        const FFMS_TrackTimeBase *timebase = FFMS_GetTimeBase( h->track );
        int64_t timebase_num = timebase->Num;
        int64_t timebase_den = timebase->Den * 1000;
        h->reduce_pts = 0;

        while( timebase_num > UINT32_MAX || timebase_den > INT32_MAX )
        {
            timebase_num >>= 1;
            timebase_den >>= 1;
            h->reduce_pts++;
        }
        info->timebase_num = timebase_num;
        info->timebase_den = timebase_den;
    }

    *p_handle = h;
    return 0;
}

static int picture_alloc( cli_pic_t *pic, int csp, int width, int height )
{
    if( x264_cli_pic_alloc( pic, csp, width, height ) )
        return -1;
    pic->img.planes = 4;
    return 0;
}

static int read_frame( cli_pic_t *pic, hnd_t handle, int i_frame )
{
    ffms_hnd_t *h = handle;
    if( i_frame >= h->num_frames )
        return -1;
    FFMS_ErrorInfo e;
    e.BufferSize = 0;
    const FFMS_Frame *frame = FFMS_GetFrame( h->video_source, i_frame, &e );
    FAIL_IF_ERROR( !frame, "could not read frame %d \n", i_frame )

    memcpy( pic->img.stride, frame->Linesize, sizeof(pic->img.stride) );
    memcpy( pic->img.plane, frame->Data, sizeof(pic->img.plane) );

    if( h->vfr_input )
    {
        const FFMS_FrameInfo *info = FFMS_GetFrameInfo( h->track, i_frame );
        FAIL_IF_ERROR( info->PTS == AV_NOPTS_VALUE, "invalid timestamp. "
                       "Use --force-cfr and specify a framerate with --fps\n" )

        pic->pts = info->PTS >> h->reduce_pts;
        pic->duration = 0;
    }
    return 0;
}

static void picture_clean( cli_pic_t *pic )
{
    memset( pic, 0, sizeof(cli_pic_t) );
}

static int close_file( hnd_t handle )
{
    ffms_hnd_t *h = handle;
    FFMS_DestroyVideoSource( h->video_source );
#if HAVE_AUDIO
    free( h->filename );
#endif
    free( h );
    return 0;
}

#if HAVE_AUDIO
static hnd_t open_audio( hnd_t handle, int track )
{
    ffms_hnd_t *h = handle;
    if( !x264_is_regular_file_path( h->filename ) )
    {
        x264_cli_log( "ffms", X264_LOG_WARNING, "reading audio from non-regular files is not implemented yet.\n" );
        return 0;
    }
    if( !h->has_audio )
        return 0;
    return x264_audio_open_from_file( NULL, h->filename, track );
}

const cli_input_t ffms_input = { open_file, picture_alloc, read_frame, NULL, picture_clean, close_file, open_audio };
#else
const cli_input_t ffms_input = { open_file, picture_alloc, read_frame, NULL, picture_clean, close_file };
#endif
