/*
 * ALSA facilities
 * 
 * Copyright (c) 2021 Fabio Michelini (github)
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies. 
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * compile with: gcc -o alsawrapper alsawrapper.c -lasound -lm
*/


#ifndef ALSAWRAPPER_H_
#define ALSAWRAPPER_H_


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <alsa/asoundlib.h>


int aw_handle_err (const char* msg);


/*============================================================================
                sample parsers
============================================================================*/


typedef signed char aw_sample_S8_t;
typedef signed short int aw_sample_S16_LE_t;
typedef signed long int aw_sample_S32_LE_t;

double long aw_parser_S8 (char** p_p_sample);
double long aw_parser_S16_LE (char** p_p_sample);
double long aw_parser_S32_LE (char** p_p_sample);


/*============================================================================
                cards, pcms, confs
============================================================================*/


#define AW_DEFAULT_NCHANNELS 2
#define AW_DEFAULT_FRAMERATE 44100
#define AW_DEFAULT_FORMAT SND_PCM_FORMAT_S16_LE
#define AW_DEFAULT_PERIOD_TIME 20000 // usec
#define AW_BUFFER_PERIOD_RATIO 4
#define AW_STAT_TIME 250000 // usec TODO: dava errore se 500000, inizialmente volume accumulato troppo alto ???
#define AW_MAX_PCMS_LENGTH 128

#define AW_MAX_NCHANNELS_LENGTH 64
#define AW_MAX_FRAMERATES_LENGTH 64
#define AW_MAX_FORMATS_LENGTH 512

typedef struct AwPcm {
    
    snd_pcm_stream_t mode;
    int nchannels[AW_MAX_NCHANNELS_LENGTH];
    int framerates[AW_MAX_FRAMERATES_LENGTH];
    snd_pcm_format_t formats[AW_MAX_FORMATS_LENGTH];
    snd_pcm_uframes_t period_size_min[32];
    snd_pcm_uframes_t period_size_max[32];
    snd_pcm_uframes_t buffer_size_min[32];
    snd_pcm_uframes_t buffer_size_max[32];
    char name[32];
    char cardname[128];
    char cardlongname[256];

} AwPcm;

typedef struct AwPcmParams {
    
    double long (*p_parser) (char**);
    unsigned int nchannels;
    unsigned int framerate;
    snd_pcm_format_t format;
    int is_signed;
    int is_little_endian;
    unsigned int nominal_bits;
    unsigned int real_bits;
    unsigned int samplesize;
    unsigned int samplerate;
    unsigned int framesize;
    unsigned int byterate;
    signed long int max; 
    float max_log;
    snd_pcm_uframes_t period_size;
    snd_pcm_uframes_t buffer_size;
    char description[512];

} AwPcmParams;

#define AW_POPULAR_FRAMERATES_LENGTH 8

static const unsigned int AW_POPULAR_FRAMERATES[AW_POPULAR_FRAMERATES_LENGTH] = {
    
    8000,
    11025,
    22050,
    44100,
    48000,
    88200,
    96000,
    192000
};

#define AW_POPULAR_FORMATS_LENGTH 5

static const snd_pcm_format_t AW_POPULAR_FORMATS[AW_POPULAR_FORMATS_LENGTH] = {
    
    SND_PCM_FORMAT_S8,
    SND_PCM_FORMAT_S16_LE,
    SND_PCM_FORMAT_U16_LE, 	
    SND_PCM_FORMAT_S24_LE, 	
    SND_PCM_FORMAT_S32_LE
};

int aw_get_pcm_devices (AwPcm aw_pcms[], unsigned int* p_aw_pcms_length);

int aw_print_pcms (AwPcm aw_pcms[], unsigned int aw_pcms_length);

int aw_print_pcm (AwPcm* p_aw_pcm);


/*============================================================================
                pcm parameters
============================================================================*/


int aw_set_params (snd_pcm_t* p_pcm, AwPcmParams* p_params);

int aw_print_params (AwPcmParams hw_params);


/*============================================================================
                record cycle and compute
============================================================================*/


typedef enum {

    AW_STOPPED = 0, 
    AW_PREPARING = 1,
    AW_MONITORING = 2, 
    AW_RECORDING = 3, 
    AW_PAUSED = 4,
    AW_STOPPING = 5 

} aw_record_state_t; 

typedef struct AwComputeStruct {

    float* avgs_queue;
    unsigned int avgs_queue_length;
    unsigned int avgs_queue_start;
    unsigned int avgs_queue_end;
    float* avg_power; 
    float* avg_log;
    float* max;
    int* clip;

} AwComputeStruct;

int aw_build_compute_struct (AwPcmParams hw_params, AwComputeStruct* p_ss);

float aw_queue_cycle (AwComputeStruct* p_ss, unsigned int channel_i, float entry);

int aw_compute (void* p_buffer, AwPcmParams* p_params, AwComputeStruct* p_ss);

int aw_cycle (snd_pcm_t* p_pcm, AwPcmParams* p_hw_params, FILE* p_f, AwComputeStruct* p_ss, aw_record_state_t* p_state);

int aw_record (const char* device_name, unsigned int nchannels, unsigned int framerate, snd_pcm_format_t format, const char* filepath, AwComputeStruct* p_ss, aw_record_state_t* p_state);


/*============================================================================
                wave save, mp3 transcode
============================================================================*/


#define AW_DEFAULT_SAVE_FORMAT "wav"

int aw_save_and_clean (const char* srcpath, const char* dstpath);

int aw_transcode_to_mp3 (const char* srcpath, const char* dstpath);


/*============================================================================
                threads
============================================================================*/


typedef struct aw_thread_struct_t
{
    snd_pcm_t* p_pcm;
    AwPcmParams* p_hw_params;
    FILE** p_p_f;
    AwComputeStruct* p_ss;
    aw_record_state_t* p_state; 
    
} aw_thread_struct_t;

void* aw_thread_func (void* p_thread_struct);

#endif  // ALSAWRAPPER_H_