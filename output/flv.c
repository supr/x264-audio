/*****************************************************************************
 * flv.c:
 *****************************************************************************
 * Copyright (C) 2009 Kieran Kunhya <kieran@kunhya.com>
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
 *****************************************************************************/

#include "muxers.h"
#include "flv_bytestream.h"
#include "audio/audio_internal.h"

#define CHECK(x)\
do {\
    if( (x) < 0 )\
        return -1;\
} while( 0 )


typedef struct
{
    int header;
    int codecid;
    int samplesize;
    int samplerate;
    int stereo;
    uint8_t *extradata;
    int extradata_size;
} flv_audio_hnd_t;

typedef struct
{
    flv_buffer *c;

    uint8_t *sei;
    int sei_len;

    int64_t i_fps_num;
    int64_t i_fps_den;
    int64_t i_framenum;

    uint64_t i_framerate_pos;
    uint64_t i_duration_pos;
    uint64_t i_filesize_pos;
    uint64_t i_bitrate_pos;

    uint8_t b_write_length;
    int64_t i_prev_dts;
    int64_t i_prev_pts;

    int i_timebase_num;
    int i_timebase_den;
    int b_vfr_input;

    unsigned start;

    audio_hnd_t *audio;
    flv_audio_hnd_t *audiodata;
} flv_hnd_t;

static int write_header( flv_buffer *c )
{
    x264_put_tag( c, "FLV" ); // Signature
    x264_put_byte( c, 1 );    // Version
    x264_put_byte( c, 5 );    // Video + Audio
    x264_put_be32( c, 9 );    // DataOffset
    x264_put_be32( c, 0 );    // PreviousTagSize0

    return flv_flush_data( c );
}

static int open_file( char *psz_filename, hnd_t *p_handle )
{
    flv_hnd_t *p_flv = malloc( sizeof(*p_flv) );
    *p_handle = NULL;
    if( !p_flv )
        return -1;
    memset( p_flv, 0, sizeof(*p_flv) );

    p_flv->c = flv_create_writer( psz_filename );
    if( !p_flv->c )
        return -1;

    CHECK( write_header( p_flv->c ) );
    *p_handle = p_flv;

    return 0;
}

static int set_param( hnd_t handle, x264_param_t *p_param )
{
    flv_hnd_t *p_flv = handle;
    flv_buffer *c = p_flv->c;

    x264_put_byte( c, FLV_TAG_TYPE_META ); // Tag Type "script data"

    int start = c->d_cur;
    x264_put_be24( c, 0 ); // data length
    x264_put_be24( c, 0 ); // timestamp
    x264_put_be32( c, 0 ); // reserved

    x264_put_byte( c, AMF_DATA_TYPE_STRING );
    x264_put_amf_string( c, "onMetaData" );

    x264_put_byte( c, AMF_DATA_TYPE_MIXEDARRAY );
    x264_put_be32( c, 7 );

    x264_put_amf_string( c, "width" );
    x264_put_amf_double( c, p_param->i_width );

    x264_put_amf_string( c, "height" );
    x264_put_amf_double( c, p_param->i_height );

    x264_put_amf_string( c, "framerate" );

    if( !p_param->b_vfr_input )
        x264_put_amf_double( c, (double)p_param->i_fps_num / p_param->i_fps_den );
    else
    {
        p_flv->i_framerate_pos = c->d_cur + c->d_total + 1;
        x264_put_amf_double( c, 0 ); // written at end of encoding
    }

    x264_put_amf_string( c, "videocodecid" );
    x264_put_amf_double( c, FLV_CODECID_H264 );

    x264_put_amf_string( c, "duration" );
    p_flv->i_duration_pos = c->d_cur + c->d_total + 1;
    x264_put_amf_double( c, 0 ); // written at end of encoding

    x264_put_amf_string( c, "filesize" );
    p_flv->i_filesize_pos = c->d_cur + c->d_total + 1;
    x264_put_amf_double( c, 0 ); // written at end of encoding

    x264_put_amf_string( c, "videodatarate" );
    p_flv->i_bitrate_pos = c->d_cur + c->d_total + 1;
    x264_put_amf_double( c, 0 ); // written at end of encoding

    if( p_flv->audio )
    {
        assert( p_flv->audio->opaque );
        flv_audio_hnd_t *a_flv = ( (flv_hnd_t*) p_flv->audio->opaque )->audiodata;
        x264_put_amf_string( c, "audiocodecid" );
        x264_put_amf_double( c, a_flv->codecid >> FLV_AUDIO_CODECID_OFFSET );
        x264_put_amf_string( c, "audiosamplesize" );
        x264_put_amf_double( c, a_flv->samplesize );
        x264_put_amf_string( c, "audiosamplerate" );
        x264_put_amf_double( c, a_flv->samplerate );
        x264_put_amf_string( c, "stereo" );
        x264_put_amf_bool  ( c, a_flv->stereo );
    }

    x264_put_amf_string( c, "" );
    x264_put_byte( c, AMF_END_OF_OBJECT );

    unsigned length = c->d_cur - start;
    rewrite_amf_be24( c, length - 10, start );

    x264_put_be32( c, length + 1 ); // tag length

    p_flv->i_fps_num = p_param->i_fps_num;
    p_flv->i_fps_den = p_param->i_fps_den;
    p_flv->i_timebase_num = p_param->i_timebase_num;
    p_flv->i_timebase_den = p_param->i_timebase_den;
    p_flv->b_vfr_input = p_param->b_vfr_input;

    return 0;
}

static int write_headers( hnd_t handle, x264_nal_t *p_nal )
{
    flv_hnd_t *p_flv = handle;
    flv_audio_hnd_t *a_flv = ( (flv_hnd_t*) p_flv->audio->opaque )->audiodata;
    flv_buffer *c = p_flv->c;

    int sps_size = p_nal[0].i_payload;
    int pps_size = p_nal[1].i_payload;
    int sei_size = p_nal[2].i_payload;

    // SEI
    /* It is within the spec to write this as-is but for
     * mplayer/ffmpeg playback this is deferred until before the first frame */

    p_flv->sei = malloc( sei_size );
    if( !p_flv->sei )
        return -1;
    p_flv->sei_len = sei_size;

    memcpy( p_flv->sei, p_nal[2].p_payload, sei_size );

    // SPS
    uint8_t *sps = p_nal[0].p_payload + 4;

    x264_put_byte( c, FLV_TAG_TYPE_VIDEO );
    x264_put_be24( c, 0 ); // rewrite later
    x264_put_be24( c, 0 ); // timestamp
    x264_put_byte( c, 0 ); // timestamp extended
    x264_put_be24( c, 0 ); // StreamID - Always 0
    p_flv->start = c->d_cur; // needed for overwriting length

    x264_put_byte( c, 7 | FLV_FRAME_KEY ); // Frametype and CodecID
    x264_put_byte( c, 0 ); // AVC sequence header
    x264_put_be24( c, 0 ); // composition time

    x264_put_byte( c, 1 );      // version
    x264_put_byte( c, sps[1] ); // profile
    x264_put_byte( c, sps[2] ); // profile
    x264_put_byte( c, sps[3] ); // level
    x264_put_byte( c, 0xff );   // 6 bits reserved (111111) + 2 bits nal size length - 1 (11)
    x264_put_byte( c, 0xe1 );   // 3 bits reserved (111) + 5 bits number of sps (00001)

    x264_put_be16( c, sps_size - 4 );
    flv_append_data( c, sps, sps_size - 4 );

    // PPS
    x264_put_byte( c, 1 ); // number of pps
    x264_put_be16( c, pps_size - 4 );
    flv_append_data( c, p_nal[1].p_payload + 4, pps_size - 4 );

    // rewrite data length info
    unsigned length = c->d_cur - p_flv->start;
    rewrite_amf_be24( c, length, p_flv->start - 10 );
    x264_put_be32( c, length + 11 ); // Last tag size

    if( a_flv && a_flv->codecid == FLV_CODECID_AAC && a_flv->extradata_size )
    {
        x264_put_byte( c, FLV_TAG_TYPE_AUDIO );
        x264_put_be24( c, 2 + a_flv->extradata_size );
        x264_put_be24( c, 0 );
        x264_put_byte( c, 0 );
        x264_put_be24( c, 0 );

        x264_put_byte( c, a_flv->header );
        x264_put_byte( c, 0 );
        flv_append_data( c, a_flv->extradata, a_flv->extradata_size );
        x264_put_be32( c, 11 + 2 + a_flv->extradata_size );
    }

    CHECK( flv_flush_data( c ) );

    return sei_size + sps_size + pps_size;
}

static int write_audio( hnd_t haud, int64_t dts, uint8_t *data, int len )
{
    assert( haud && data );
    audio_hnd_t *audio = haud;
    flv_hnd_t *p_flv = audio->opaque;
    assert( p_flv->audio );
    flv_audio_hnd_t *a_flv = p_flv->audiodata;
    flv_buffer *c = p_flv->c;

    if( !a_flv )
    {
        fprintf( stderr, "flv [error]: attempt to write audio without initialization!\n" );
        return -1;
    }

    // Convert to miliseconds
    dts = from_time_base( dts * 1000, audio->time_base );
    int aac = a_flv->codecid == FLV_CODECID_AAC;

    x264_put_byte( c, FLV_TAG_TYPE_AUDIO );
    x264_put_be24( c, 1 + aac + len ); // size
    x264_put_be24( c, (int32_t) dts ); // DTS
    x264_put_byte( c, (int32_t) dts >> 24 ); // DTS high 8 bits
    x264_put_be24( c, 0 );

    x264_put_byte( c, a_flv->header );

    if( aac )
        x264_put_byte( c, 1 );
    flv_append_data( c, data, len );

    x264_put_be32( c, 11 + 1 + aac + len );

    CHECK( flv_flush_data( c ) );

    return 1 + len + aac;
}

static cli_audio_t flv_audio_output = {
    .write_audio      = write_audio
};

static int init_audio( hnd_t muxer, hnd_t haud )
{
    assert( muxer && haud );
    flv_hnd_t *p_flv = muxer;
    audio_hnd_t *audio = haud;
    assert( !audio->muxer );
    if( !audio->enc )
    {
        fprintf( stderr, "flv [error]: cannot initialize audio for uninitialized encoder\n" );
        return 0;
    }
    audio_info_t *info = audio->enc->info;
    const char *codec_name = info->codec_name;

    audio->muxer          = p_flv->audio     = calloc( 1, sizeof( audio_hnd_t ) );
    p_flv->audio->opaque  = p_flv;
    flv_audio_hnd_t *o = p_flv->audiodata = malloc( sizeof( flv_audio_hnd_t ) );

    p_flv->audio->self      = &flv_audio_output;
    p_flv->audio->time_base = audio->enc->time_base;
    assert( p_flv->audio->time_base );

    int header = 0;
    if( !strcmp( codec_name, "mp3" ) || !strcmp( codec_name, "libmp3lame" ) )
        o->codecid = FLV_CODECID_MP3;
    else if( !strcmp( codec_name, "aac" ) || !strcmp( codec_name, "libfaac" ) )
    {
        o->codecid        = FLV_CODECID_AAC;
        o->extradata_size = info->extradata_size;
        o->extradata      = info->extradata;
    }
    else if( !strcmp( codec_name, "raw" ) )
        o->codecid = FLV_CODECID_RAW;
    else if( !strcmp( codec_name, "adpcm_swf" ) )
        o->codecid = FLV_CODECID_ADPCM;
    else
    {
        fprintf( stderr, "flv [error]: unsupported audio codec %s\n", codec_name );
        goto error;
    }

    header |= o->codecid;

    o->samplerate = info->samplerate;
    o->samplesize = info->samplesize;
    o->stereo     = info->channels == 2;

    switch( info->samplerate )
    {
    case 5512:
    case 8000:
        header |= FLV_SAMPLERATE_SPECIAL;
        break;
    case 11025:
        header |= FLV_SAMPLERATE_11025HZ;
        break;
    case 22050:
        header |= FLV_SAMPLERATE_22050HZ;
        break;
    case 44100:
        header |= FLV_SAMPLERATE_44100HZ;
        break;
    default:
        fprintf( stderr, "flv [error]: unsupported %dhz sample rate\n", info->samplerate );
        goto error;
    }

    switch( info->samplesize )
    {
    case 1:
        header |= FLV_SAMPLESSIZE_8BIT;
        break;
    case 2:
        header |= FLV_SAMPLESSIZE_16BIT;
        break;
    default:
        fprintf( stderr, "flv [error]: %d-bit audio not supported\n", info->samplesize * 8 );
        goto error;
    }

    switch( info->channels )
    {
    case 1:
        header |= FLV_MONO;
        break;
    case 2:
        header |= FLV_STEREO;
        break;
    default:
        fprintf( stderr, "flv [error]: %d-channel audio not supported\n", info->channels );
        goto error;
    }

    o->header = header;

    return 1;

error:
    free( o );
    free( p_flv->audio );
    p_flv->audio = NULL;
    audio->muxer = NULL; // alias to p_flv->audio

    return 0;
}

static int write_frame( hnd_t handle, uint8_t *p_nalu, int i_size, x264_picture_t *p_picture )
{
    flv_hnd_t *p_flv = handle;
    flv_buffer *c = p_flv->c;

    int64_t dts = (int64_t)( (p_picture->i_dts * 1000 * ((double)p_flv->i_timebase_num / p_flv->i_timebase_den)) + 0.5 );
    int64_t cts = (int64_t)( (p_picture->i_pts * 1000 * ((double)p_flv->i_timebase_num / p_flv->i_timebase_den)) + 0.5 );
    int64_t offset = cts - dts;

    if( p_flv->i_framenum )
    {
        int64_t prev_dts = (int64_t)( (p_flv->i_prev_dts * 1000 * ((double)p_flv->i_timebase_num / p_flv->i_timebase_den)) + 0.5 );
        int64_t prev_cts = (int64_t)( (p_flv->i_prev_pts * 1000 * ((double)p_flv->i_timebase_num / p_flv->i_timebase_den)) + 0.5 );
        if( prev_dts == dts )
        {
            double fps = ((double)p_flv->i_timebase_den / p_flv->i_timebase_num) / (p_picture->i_dts - p_flv->i_prev_dts);
            fprintf( stderr, "flv [warning]: duplicate DTS %"PRId64" generated by rounding\n"
                             "               current internal decoding framerate: %.6f fps\n", dts, fps );
        }
        if( prev_cts == cts )
        {
            double fps = ((double)p_flv->i_timebase_den / p_flv->i_timebase_num) / (p_picture->i_pts - p_flv->i_prev_pts);
            fprintf( stderr, "flv [warning]: duplicate CTS %"PRId64" generated by rounding\n"
                             "               current internal composition framerate: %.6f fps\n", cts, fps );
        }
    }
    p_flv->i_prev_dts = p_picture->i_dts;
    p_flv->i_prev_pts = p_picture->i_pts;

    // A new frame - write packet header
    x264_put_byte( c, FLV_TAG_TYPE_VIDEO );
    x264_put_be24( c, 0 ); // calculated later
    x264_put_be24( c, dts );
    x264_put_byte( c, dts >> 24 );
    x264_put_be24( c, 0 );

    p_flv->start = c->d_cur;
    x264_put_byte( c, p_picture->b_keyframe ? FLV_FRAME_KEY : FLV_FRAME_INTER );
    x264_put_byte( c, 1 ); // AVC NALU
    x264_put_be24( c, offset );

    if( p_flv->sei )
    {
        flv_append_data( c, p_flv->sei, p_flv->sei_len );
        free( p_flv->sei );
        p_flv->sei = NULL;
    }
    flv_append_data( c, p_nalu, i_size );

    unsigned length = c->d_cur - p_flv->start;
    rewrite_amf_be24( c, length, p_flv->start - 10 );
    x264_put_be32( c, 11 + length ); // Last tag size
    CHECK( flv_flush_data( c ) );

    p_flv->i_framenum++;

    return i_size;
}

static void rewrite_amf_double( FILE *fp, uint64_t position, double value )
{
    uint64_t x = endian_fix64( dbl2int( value ) );
    fseek( fp, position, SEEK_SET );
    fwrite( &x, 8, 1, fp );
}

static int close_file( hnd_t handle, int64_t largest_pts, int64_t second_largest_pts )
{
    flv_hnd_t *p_flv = handle;
    flv_buffer *c = p_flv->c;

    CHECK( flv_flush_data( c ) );

    double total_duration = (double)(2 * largest_pts - second_largest_pts) * p_flv->i_timebase_num / p_flv->i_timebase_den;

    if( x264_is_regular_file( c->fp ) )
    {
        double framerate;
        uint64_t filesize = ftell( c->fp );

        if( p_flv->i_framerate_pos )
        {
            framerate = (double)p_flv->i_framenum / total_duration;
            rewrite_amf_double( c->fp, p_flv->i_framerate_pos, framerate );
        }

        rewrite_amf_double( c->fp, p_flv->i_duration_pos, total_duration );
        rewrite_amf_double( c->fp, p_flv->i_filesize_pos, filesize );
        rewrite_amf_double( c->fp, p_flv->i_bitrate_pos, filesize * 8 / ( total_duration * 1000 ) );
    }

    fclose( c->fp );
    free( p_flv );
    free( c );

    return 0;
}

const cli_output_t flv_output = { open_file, set_param, write_headers, write_frame, close_file, init_audio };
