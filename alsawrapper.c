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


#include "alsawrapper.h"



int aw_handle_err (const char* msg)
{
    fprintf (stderr, "%s\n", msg);
    return -1;
}


/*============================================================================
                sample parsers
============================================================================*/


double long aw_parser_S8 (char** p_p_sample)
{
    double long value;

    value = (double long) *((aw_sample_S8_t*) (*p_p_sample));
    *p_p_sample +=1;

    return value;
}

double long aw_parser_S16_LE (char** p_p_sample)
{
    double long value;

    value = (double long) *((aw_sample_S16_LE_t*) (*p_p_sample));
    *p_p_sample +=2;

    return value;
}

double long aw_parser_S32_LE (char** p_p_sample)
{
    double long value;

    value = (double long) *((aw_sample_S32_LE_t*) (*p_p_sample));
    *p_p_sample +=4;

    return value;
}


/*============================================================================
                cards, pcms, confs
============================================================================*/


int aw_get_pcm_devices (AwPcm aw_pcms[], unsigned int* p_aw_pcms_length)
{
    int err;

    int card_i;
    int dev_i;

    unsigned int i;
    unsigned int nchannels;
    unsigned int framerate_i;
    unsigned int format_i;
    snd_pcm_stream_t mode;
    snd_pcm_format_mask_t* p_mask;
    char card_name[32];
    char dev_name[32];
    char* card_human_name = NULL;
    char* card_human_longname = NULL;
    snd_ctl_t* p_ctl;
    snd_pcm_t* p_pcm;
    snd_pcm_hw_params_t* p_hw_params;
    int dir;
    unsigned int min, max;

    *p_aw_pcms_length = 0;

    if ((err = snd_pcm_hw_params_malloc (&p_hw_params)) < 0)
        return aw_handle_err (snd_strerror(err));                

    card_i = -1;
    
    while (1)
    {
        if ((err = snd_card_next (&card_i)) < 0 || card_i < 0) break;
        
        snd_card_get_name(card_i, &card_human_name);
        snd_card_get_longname(card_i, &card_human_longname);   
        sprintf(card_name, "hw:%d", card_i);
        
        if ((err = snd_ctl_open(&p_ctl, card_name, 0)) < 0 || card_i < 0) continue;  
        
        dev_i = -1;

        while (1)
        {
            if ((err = snd_ctl_pcm_next_device (p_ctl, &dev_i)) < 0 || dev_i < 0) break;
            
            snprintf(dev_name, sizeof dev_name, "hw:%d,%d", card_i, dev_i);   
            
            mode = SND_PCM_STREAM_CAPTURE;

            while (1)
            {
                if ((err = snd_pcm_open (&p_pcm, dev_name, mode, SND_PCM_ASYNC)) == 0)
                {   
                    if ((err = snd_pcm_hw_params_any (p_pcm, p_hw_params)) < 0)
                        return aw_handle_err (snd_strerror(err)); // TODO: o continue on next pcm/card ???
                        
                    aw_pcms[*p_aw_pcms_length].mode = mode;

                    if ((err = snd_pcm_hw_params_get_channels_min(p_hw_params, &min)) < 0) 
                        continue;
                        
                    if ((err = snd_pcm_hw_params_get_channels_max(p_hw_params, &max)) < 0)
                        continue;

                    i = 0;
                    for (nchannels = min; nchannels <= max; nchannels++)
                    
                        if ((err = snd_pcm_hw_params_test_channels(p_pcm, p_hw_params, nchannels)) == 0)
                        {
                            aw_pcms[*p_aw_pcms_length].nchannels[i] = nchannels;
                            i++;
                        }                 
                    aw_pcms[*p_aw_pcms_length].nchannels[i] = -1;

                    if ((err = snd_pcm_hw_params_get_rate_min( p_hw_params, &min, &dir)) < 0) 
                        continue;
                        
                    if ((err = snd_pcm_hw_params_get_rate_max(p_hw_params, &max, &dir)) < 0)
                        continue;

                    i = 0;
                    for (framerate_i = 0; framerate_i < AW_POPULAR_FRAMERATES_LENGTH; framerate_i++)

                        if (AW_POPULAR_FRAMERATES[framerate_i] >= min && AW_POPULAR_FRAMERATES[framerate_i] <= max)

                            if ((err = snd_pcm_hw_params_test_rate(p_pcm, p_hw_params, AW_POPULAR_FRAMERATES[framerate_i], 0)) == 0)
                            {
                                aw_pcms[*p_aw_pcms_length].framerates[i] = AW_POPULAR_FRAMERATES[framerate_i];
                                i++;
                            }                            
                    aw_pcms[*p_aw_pcms_length].framerates[i] = -1;

                    i = 0;
                    for (format_i = 0; format_i < AW_POPULAR_FORMATS_LENGTH; format_i++)
                    {
                        if ((err = snd_pcm_hw_params_test_format(p_pcm, p_hw_params, AW_POPULAR_FORMATS[format_i])) == 0)
                        {
                            aw_pcms[*p_aw_pcms_length].formats[i] = AW_POPULAR_FORMATS[format_i];
                            i++;
                        }
                    }
                    aw_pcms[*p_aw_pcms_length].formats[i] = SND_PCM_FORMAT_UNKNOWN;
                    
                    snprintf(aw_pcms[*p_aw_pcms_length].name, sizeof aw_pcms[*p_aw_pcms_length].name, "%s", dev_name);
                    snprintf(aw_pcms[*p_aw_pcms_length].cardname, sizeof aw_pcms[*p_aw_pcms_length].cardname, "%s", card_human_name);
                    snprintf(aw_pcms[*p_aw_pcms_length].cardlongname, sizeof aw_pcms[*p_aw_pcms_length].cardlongname, "%s", card_human_longname);
                    
                    // sprintf(aw_pcms[*p_aw_pcms_length].info, "%s", info);

                    (*p_aw_pcms_length)++;
                    snd_pcm_close(p_pcm);
                }

                if (mode == SND_PCM_STREAM_PLAYBACK) break;
                mode = SND_PCM_STREAM_PLAYBACK;
            }
        }
        snd_ctl_close(p_ctl);
    }
    snd_pcm_hw_params_free (p_hw_params);

    return 0;
}

int aw_print_pcms (AwPcm aw_pcms[], unsigned int aw_pcms_length)
{
    int i;
    int j;

    printf("There are %d pcms\n", aw_pcms_length);

    for (i = 0; i < aw_pcms_length; i++)
    {
        printf("\nPCM: %s\n", (*(aw_pcms + i)).name);
        printf("\n    card name: %s", (*(aw_pcms + i)).cardname);
        printf("\n    card long name: %s\n", (*(aw_pcms + i)).cardlongname);
        if ((*(aw_pcms + i)).mode == SND_PCM_STREAM_PLAYBACK) 
            printf("    mode:  playback");
        else
            printf("    mode:  capture");
        
        printf("\n    nchannels:  ");
        j = 0;
        while ((*(aw_pcms + i)).nchannels[j] != -1)
        {
            printf("%d  ", (*(aw_pcms + i)).nchannels[j]);
            j++;
        }       

        printf("\n    framerates:  ");
        j = 0;
        while ((*(aw_pcms + i)).framerates[j] != -1)
        {
            printf("%d  ", (*(aw_pcms + i)).framerates[j]);
            j++;
        }       

        printf("\n    formats:  ");
        j = 0;
        while ((*(aw_pcms + i)).formats[j] != SND_PCM_FORMAT_UNKNOWN)
        {
            printf("%s (%s)  ", snd_pcm_format_name((*(aw_pcms + i)).formats[j]), snd_pcm_format_description((*(aw_pcms + i)).formats[j]));
            j++;
        }       
        printf("\n"); 
    }
    return 0;
}

int aw_print_pcm (AwPcm* p_aw_pcm) 
{
    int j;
    
    printf("\nPCM: %s\n", (*p_aw_pcm).name);
    printf("\n    card name: %s", (*p_aw_pcm).cardname);
    printf("\n    card long name: %s\n", (*p_aw_pcm).cardlongname);
    
    if ((*p_aw_pcm).mode == SND_PCM_STREAM_PLAYBACK) 
        printf("    mode:  playback");
    else
        printf("    mode:  capture");
    
    printf("\n    nchannels:  ");
    j = 0;
    while ((*p_aw_pcm).nchannels[j] != -1)
    {
        printf("%d  ", (*p_aw_pcm).nchannels[j]);
        j++;
    }       

    printf("\n    framerates:  ");
    j = 0;
    while ((*p_aw_pcm).framerates[j] != -1)
    {
        printf("%d  ", (*p_aw_pcm).framerates[j]);
        j++;
    }       

    printf("\n    formats:  ");
    j = 0;
    while ((*p_aw_pcm).formats[j] != SND_PCM_FORMAT_UNKNOWN)
    {
        printf("%s (%s)  ", snd_pcm_format_name((*p_aw_pcm).formats[j]), snd_pcm_format_description((*p_aw_pcm).formats[j]));
        j++;
    }       
    printf("\n"); 
    
    return 0;
}

/*============================================================================
                pcm parameters
============================================================================*/


int aw_set_params (snd_pcm_t* p_pcm, AwPcmParams* p_hw_params)
{
    int err;
    int dir = 0;
    snd_pcm_hw_params_t* p_alsa_hw_params;
    unsigned int min_buffer_time;
    unsigned int max_buffer_time;
    unsigned int min_period_time;
    unsigned int max_period_time;
    unsigned int period_time = AW_DEFAULT_PERIOD_TIME;
    
    if ((err = snd_pcm_hw_params_malloc (&p_alsa_hw_params)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    if ((err = snd_pcm_hw_params_any (p_pcm, p_alsa_hw_params)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    if ((err = snd_pcm_hw_params_set_access (p_pcm, p_alsa_hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    if ((err = snd_pcm_hw_params_set_format (p_pcm, p_alsa_hw_params, (*p_hw_params).format)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    if ((err = snd_pcm_hw_params_set_rate (p_pcm, p_alsa_hw_params, (*p_hw_params).framerate, 0)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    if ((err = snd_pcm_hw_params_set_channels (p_pcm, p_alsa_hw_params, (*p_hw_params).nchannels)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    snd_pcm_hw_params_get_period_time_min (p_alsa_hw_params, &min_period_time, &dir);
    snd_pcm_hw_params_get_period_time_max (p_alsa_hw_params, &max_period_time, &dir);
    snd_pcm_hw_params_get_buffer_time_min (p_alsa_hw_params, &min_buffer_time, &dir);
    snd_pcm_hw_params_get_buffer_time_max (p_alsa_hw_params, &max_buffer_time, &dir);

    // TODO: check respect with DEFAULT_PERIDO_TIME

    if ((err = snd_pcm_hw_params_set_period_time_near (p_pcm, p_alsa_hw_params, &period_time, &dir)) < 0)
        return aw_handle_err (snd_strerror (err));

    if ((err = snd_pcm_hw_params_get_period_size (p_alsa_hw_params, &(*p_hw_params).period_size, &dir)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    (*p_hw_params).buffer_size = (*p_hw_params).period_size * AW_BUFFER_PERIOD_RATIO;

    if ((err = snd_pcm_hw_params_set_buffer_size (p_pcm, p_alsa_hw_params, (*p_hw_params).buffer_size)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    if ((err = snd_pcm_hw_params (p_pcm, p_alsa_hw_params)) < 0)
        return aw_handle_err (snd_strerror(err));

    /* complete params */

    if ((*p_hw_params).format == SND_PCM_FORMAT_S8)
    {
        (*p_hw_params).p_parser = &aw_parser_S8;

    } else if ((*p_hw_params).format == SND_PCM_FORMAT_S16_LE) {

        (*p_hw_params).p_parser = &aw_parser_S16_LE;
    
    } else if ((*p_hw_params).format == SND_PCM_FORMAT_S16_LE) {

        (*p_hw_params).p_parser = &aw_parser_S32_LE;
    
    } else if ((*p_hw_params).format == SND_PCM_FORMAT_S16_LE) {

        (*p_hw_params).p_parser = &aw_parser_S32_LE;
    
    } else {

        printf("format not recognized, guess S16_LE\n"); // TODO: togliere ???
        (*p_hw_params).p_parser = &aw_parser_S16_LE;
        return -1;
    }

    (*p_hw_params).nominal_bits = snd_pcm_format_width ((*p_hw_params).format);
    (*p_hw_params).real_bits = snd_pcm_format_physical_width ((*p_hw_params).format);
    (*p_hw_params).max = (1 << ((*p_hw_params).nominal_bits - 1)) - 1;
    (*p_hw_params).max_log = 20 * log10 ((*p_hw_params).max);
    (*p_hw_params).samplesize = (*p_hw_params).real_bits / 8;
    (*p_hw_params).samplerate = (*p_hw_params).nchannels * (*p_hw_params).framerate;    
    (*p_hw_params).framesize = (*p_hw_params).nchannels * (*p_hw_params).samplesize;
    (*p_hw_params).byterate = (*p_hw_params).samplerate * (*p_hw_params).samplesize;
    snprintf((*p_hw_params).description, sizeof (*p_hw_params).description, "channels %d framerate %d format %s (%s)", (*p_hw_params).nchannels, (*p_hw_params).framerate, snd_pcm_format_name((*p_hw_params).format), snd_pcm_format_description((*p_hw_params).format));

    snd_pcm_hw_params_free (p_alsa_hw_params);

    return 0;
}

int aw_print_params (AwPcmParams hw_params)
{   
    printf ("\nHW PARAMS\n\n");

    printf ("nchannels: %d\n", hw_params.nchannels);
    printf ("framerate: %d\n", hw_params.framerate);
    printf ("format: %d\n", hw_params.format);
    printf ("is_signed: %d\n", hw_params.is_signed);
    printf ("is_little_endian: %d\n", hw_params.is_little_endian);
    printf ("nominal_bits: %d\n", hw_params.nominal_bits);
    printf ("real_bits: %d\n", hw_params.real_bits);
    printf ("samplesize: %d\n", hw_params.samplesize);
    printf ("samplerate: %d\n", hw_params.samplerate);
    printf ("framesize: %d\n", hw_params.framesize);
    printf ("byterate: %d\n", hw_params.byterate);
    printf ("period_size: %ld\n", hw_params.period_size);
    printf ("buffer_size: %ld\n", hw_params.buffer_size);
    printf ("description: %s\n", hw_params.description);

    return 0;
}


/*============================================================================
                record cycle and compute
============================================================================*/


float aw_queue_cycle (AwComputeStruct* p_ss, unsigned int channel_i, float entry)
{
    float* p_first;
    float first;

    p_first = (*p_ss).avgs_queue + channel_i * (*p_ss).avgs_queue_length + (*p_ss).avgs_queue_start;

    first = *p_first;
    *p_first = entry;    
    ((*p_ss).avgs_queue_start == 0) ? (*p_ss).avgs_queue_start = (*p_ss).avgs_queue_length - 1 : (*p_ss).avgs_queue_start--;
    ((*p_ss).avgs_queue_end == 0) ? (*p_ss).avgs_queue_end = (*p_ss).avgs_queue_length - 1 : (*p_ss).avgs_queue_end--;

    return first;
}

int aw_build_compute_struct (AwPcmParams hw_params, AwComputeStruct* p_ss)
{
    (*p_ss).avgs_queue_length = (unsigned int) ceil((hw_params.framerate * (float) AW_STAT_TIME / 1000000) / hw_params.buffer_size);
    (*p_ss).avgs_queue_start = 0;
    (*p_ss).avgs_queue_end = (*p_ss).avgs_queue_length - 1;
    
    if (((*p_ss).avgs_queue = (float*) calloc (hw_params.nchannels * (*p_ss).avgs_queue_length, sizeof (float))) == NULL) aw_handle_err (strerror (errno));
    if (((*p_ss).avg_power = (float*) calloc (hw_params.nchannels, sizeof (float))) == NULL) aw_handle_err (strerror (errno));
    if (((*p_ss).avg_log = (float*) calloc (hw_params.nchannels, sizeof (float))) == NULL) aw_handle_err (strerror (errno));
    if (((*p_ss).max = (float*) calloc (hw_params.nchannels, sizeof (float))) == NULL) aw_handle_err (strerror (errno));
    if (((*p_ss).clip = (int*) calloc (hw_params.nchannels, sizeof (int))) == NULL) aw_handle_err (strerror (errno));

    return 0;
}

int aw_compute (void* p_buffer, AwPcmParams* p_params, AwComputeStruct* p_ss)
{
    unsigned int frame_i;
    unsigned int channel_i;
    double long value;
    double long sum_power;
    float in_avg;
    float out_avg;
    signed int (*p_sample_parser)(char**);
    char* p_sample;

    p_sample = (char*) p_buffer;
    
    for (channel_i = 0; channel_i < (*p_params).nchannels; channel_i++)
    {        
        sum_power = 0;
        
        for (frame_i = 0; frame_i < (*p_params).buffer_size; frame_i++)
        {   
            value = (*(*p_params).p_parser) (&p_sample);       
            sum_power += value * value;
            
            value = floor(abs(value) * 100.0 / (*p_params).max);            
            // if (value >= 100) printf("value [%d]: %f\n", channel_i, (float) value);
            
            if (value == 100) (*p_ss).clip[channel_i] = 1;
            if (value > (*p_ss).max[channel_i]) (*p_ss).max[channel_i] = value;
        }
        in_avg = sqrt((sum_power / (*p_params).buffer_size)) * 100.0 / (*p_params).max / (*p_ss).avgs_queue_length;        
        out_avg = aw_queue_cycle (p_ss, channel_i, in_avg);
        // if (in_avg >= 100) printf ("inavg [%d]: %f\n", channel_i, in_avg);

        (*p_ss).avg_power[channel_i] += (in_avg - out_avg);
        // if ((*p_ss).avg_power[channel_i] >= 100) printf("avg_power [%d] %f\n", channel_i, (*p_ss).avg_power[channel_i]);

        if ((*p_ss).avg_power[channel_i] > 1)
        {
            (*p_ss).avg_log[channel_i] = 20 * log10 ((*p_ss).avg_power[channel_i]) / 40 * 100;

        } else {

            (*p_ss).avg_log[channel_i] = 0;
        }
        // printf("%f (max %f) %f        ", (*p_ss).avg_power[channel_i], (*p_ss).max[channel_i], (*p_ss).avg_log[channel_i]);
    }
    // printf("\n");
    return 0;
}

int aw_cycle (snd_pcm_t* p_pcm, AwPcmParams* p_hw_params, FILE** p_p_f, AwComputeStruct* p_ss, aw_record_state_t* p_state)
{
    int i;
    int err;    
    void* p_buffer = malloc (snd_pcm_frames_to_bytes (p_pcm, (*p_hw_params).buffer_size)); // = hw_params.buffer_size * hw_params.framesize
    snd_pcm_sframes_t nframes_or_err;
    // cosa è snd_pcm_hw_params_get_export_buffer ???  
    
    i = 0;
    
    while (*p_state == AW_RECORDING || *p_state == AW_MONITORING || *p_state == AW_PAUSED)
    {
        if ((nframes_or_err = snd_pcm_readi (p_pcm, p_buffer, (*p_hw_params).buffer_size)) != (*p_hw_params).buffer_size)        
            if (snd_pcm_recover(p_pcm, nframes_or_err, 0) < 0)
                return aw_handle_err (snd_strerror (nframes_or_err));
                
        if (aw_compute (p_buffer, p_hw_params, p_ss) < 0)
            return aw_handle_err ("error in computing");
        
        if (*p_state == AW_RECORDING)
        {
            if (fwrite (p_buffer, (*p_hw_params).samplesize, (*p_hw_params).buffer_size * (*p_hw_params).nchannels, *p_p_f) < 0) // TODO: in memory option with iostream
                return aw_handle_err (strerror (err));
        }

        i++;
    }
    *p_state = AW_STOPPED;
    
    return 0;
}

int aw_record (const char* device_name, unsigned int nchannels, unsigned int framerate, snd_pcm_format_t format, const char* filepath, AwComputeStruct* p_ss, aw_record_state_t* p_state) // see snd_pcm_build_linear_format(int width, int pwidth, int unsignd, int big_endian);
{
    int err;
    snd_pcm_t* p_pcm;
    AwPcmParams hw_params;

    hw_params.nchannels = nchannels; 
    hw_params.framerate = framerate; 
    hw_params.format = format; 
    
    FILE* p_f;

    if ((err = snd_pcm_open (&p_pcm, device_name, SND_PCM_STREAM_CAPTURE, SND_PCM_ASYNC)) < 0)
        return aw_handle_err (snd_strerror(err));

    if ((err = aw_set_params (p_pcm, &hw_params)) < 0)
        return aw_handle_err ("cannot set params");  
    
    aw_build_compute_struct(hw_params, p_ss);    

    aw_print_params (hw_params);
    
    if ((p_f = fopen (filepath, "w")) == NULL)
        return aw_handle_err (strerror(errno));
    
    if ((err = snd_pcm_prepare (p_pcm)) < 0) // serve ??? già chiamata da snd_pcm_hw_params
        return aw_handle_err(snd_strerror (err));

    if ((err = snd_pcm_state (p_pcm)) != SND_PCM_STATE_PREPARED)
        return aw_handle_err(snd_strerror (err));

    *p_state = AW_RECORDING;
    
    if ((err = snd_pcm_start (p_pcm)) < 0)
        return aw_handle_err (snd_strerror(err));
    
    if ((err = aw_cycle (p_pcm, &hw_params, &p_f, p_ss, p_state)) < 0)
        return aw_handle_err ("broken reading cycle");
    
    if ((fclose(p_f)) == EOF)
        return aw_handle_err (strerror(errno));
    
    if ((err = snd_pcm_close (p_pcm)) < 0)
        return aw_handle_err (snd_strerror(err));

    return (0);
}


/*============================================================================
                threads
============================================================================*/


void* aw_thread_func (void* p_thread_struct) 
{
    aw_thread_struct_t thread_struct;

    thread_struct = *((aw_thread_struct_t*) p_thread_struct);
    
    aw_cycle (thread_struct.p_pcm,
              thread_struct.p_hw_params, 
              thread_struct.p_p_f, 
              thread_struct.p_ss, 
              thread_struct.p_state);
}



// int main()
// {
//     exit(0);
// }

// apt-get install libasound2-dev mette "asoundlib.h" in "/usr/include/alsa"
// scaricare wxwidgets e seguire compilazione e installazione
// gcc -g -Wall -shared -o alsawrapper.so -fPIC alsawrapper.c -lasound
// gcc -g -o alsawrapper -fPIC alsawrapper.c -lasound -lpthread -lm
// g++ `wx-config --cxxflags` alsarecorder.cpp alsawrapper.c -lasound -lpthread -lm -o alsarecorder `wx-config --libs`
// l=cdll.LoadLibrary("./alsawrapper.so")
// nm -D alsawrapper.so per vedere  cosa espone (contrassegnati con T)

// struct WaveHeader {
//     unsigned char riff[4];				// RIFF string
//     unsigned int overall_size	;		// overall size of file in bytes
//     unsigned char wave[4];				// WAVE string
//     unsigned char fmt_chunk_marker[4];	// format string with trailing null char
//     unsigned int length_of_fmt;			// length of the format data
//     unsigned int format_type;			// format type. 1-PCM, 3- IEEE float, 6 - 8bit A law, 7 - 8bit mu law
//     unsigned int channels;				// no.of channels
//     unsigned int sample_rate;			// sampling rate (blocks per second)
//     unsigned int byterate;				// SampleRate * NumChannels * BitsPerSample/8
//     unsigned int block_align;			// NumChannels * BitsPerSample/8
//     unsigned int bits_per_sample;		// bits per sample, 8- 8bits, 16- 16 bits etc
//     unsigned char data_chunk_header[4];	// DATA string or FLLR string
//     unsigned int data_size;				// NumSamples * NumChannels *
//     // BitsPerSample/8 - size of the
//     // next chunk that will be read

// };