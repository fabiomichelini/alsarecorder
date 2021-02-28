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
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <alsa/asoundlib.h>


int aw_handle_err (const char* msg);


/*============================================================================
                sample parsers
============================================================================*/


int32_t aw_parser_S8 (char* p_sample);
int32_t aw_parser_S16_LE (char* p_sample);
int32_t aw_parser_S32_LE (char* p_sample);


/*============================================================================
                cards, pcms, confs
============================================================================*/


#define AW_DEFAULT_NCHANNELS 2
#define AW_DEFAULT_FRAMERATE 44100
#define AW_DEFAULT_FORMAT SND_PCM_FORMAT_S16_LE
#define AW_DEFAULT_PERIOD_TIME 20000 // usec
#define AW_BUFFER_PERIOD_RATIO 4
#define AW_STAT_TIME 250000 // usec
#define AW_MAX_PCMS_LENGTH 128

#define AW_MAX_NCHANNELS_LENGTH 32
#define AW_MAX_FRAMERATES_LENGTH 32
#define AW_MAX_FORMATS_LENGTH 64


/* pcm capabilities */
typedef struct AwPcm {
    
    snd_pcm_stream_t mode;
    int8_t nchannels[AW_MAX_NCHANNELS_LENGTH];
    int32_t framerates[AW_MAX_FRAMERATES_LENGTH];
    snd_pcm_format_t formats[AW_MAX_FORMATS_LENGTH];
    snd_pcm_uframes_t period_size_min[32];
    snd_pcm_uframes_t period_size_max[32];
    snd_pcm_uframes_t buffer_size_min[32];
    snd_pcm_uframes_t buffer_size_max[32];
    char name[32];
    char cardname[128];
    char cardlongname[256];

} AwPcm;

/* pcm configuration capabilities */
typedef struct AwPcmParams {
    
    int32_t (*p_parser) (char*);
    uint8_t nchannels;
    uint32_t framerate;
    snd_pcm_format_t format;
    int is_signed;
    int is_little_endian;
    uint8_t nominal_bits;
    uint8_t real_bits;
    uint8_t samplesize;
    uint32_t samplerate;
    uint8_t framesize;
    uint32_t byterate;
    uint32_t max; 
    snd_pcm_uframes_t period_size;
    snd_pcm_uframes_t buffer_size;
    char description[512];

} AwPcmParams;

#define AW_POPULAR_FRAMERATES_LENGTH 9

static const uint32_t AW_POPULAR_FRAMERATES[AW_POPULAR_FRAMERATES_LENGTH] = {
    
    8000,
    11025,
    16000,
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

int aw_get_pcm_devices (AwPcm aw_pcms[], uint8_t* p_aw_pcms_length);

int aw_print_pcms (AwPcm aw_pcms[], uint8_t aw_pcms_length);

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
    uint16_t avgs_queue_length;
    uint16_t avgs_queue_start;
    uint16_t avgs_queue_end;
    float* avg_power; 
    float* avg_log;
    float* max;
    int* clip;

} AwComputeStruct;

int aw_build_compute_struct (AwPcmParams hw_params, AwComputeStruct* p_ss);

float aw_queue_cycle (AwComputeStruct* p_ss, int channel_i, float entry);

int aw_compute (void* p_buffer, AwPcmParams* p_params, AwComputeStruct* p_ss);

int aw_cycle (snd_pcm_t* p_pcm, AwPcmParams* p_hw_params, FILE** p_p_f, AwComputeStruct* p_ss, aw_record_state_t* p_state);

int aw_record (const char* device_name, uint8_t nchannels, uint32_t framerate, snd_pcm_format_t format, const char* filepath, AwComputeStruct* p_ss, aw_record_state_t* p_state);


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