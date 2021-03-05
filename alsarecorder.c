/*
 * ALSA recorder
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
 * compile with: gcc -rdynamic -no-pie `pkg-config --cflags gtk+-3.0` -o alsarecorder alsarecorder.c alsawrapper.c `pkg-config --libs gtk+-3.0` -lasound -lpthread -lm
*/

#include "sys/time.h"
#include "time.h"
#include "signal.h"
#include "stdlib.h"
#include "stdint.h"
#include "pwd.h"
#include "unistd.h"

#include "gtk/gtk.h"

#include "alsawrapper.h"    


/*============================================================================
    definitions and static globals
============================================================================*/



#define MAX_CHANNELS 32
#define UPDATE_TIMER_INTERVAL 120 // in msec
#define MAX_TOT_TIME 3600 // in sec
#define SAVE_TO_WAV 0
#define SAVE_TO_MP3 1
#define VU_NORMAL 0
#define VU_LOGARITHMIC 1

static AwPcm* p_aw_pcm = NULL;
static snd_pcm_t* p_pcm = NULL;
static AwPcm aw_pcms[AW_MAX_PCMS_LENGTH];
static uint8_t aw_pcms_length;
static AwPcmParams aw_pcm_params;
static uint8_t _nchannels;
static uint32_t _framerate;
static snd_pcm_format_t _format;
static aw_record_state_t state = AW_STOPPED;
static int defaultPcm = 0;
static int saveFormat = SAVE_TO_WAV;
static int vuFormat = VU_LOGARITHMIC;
static AwComputeStruct ss;
static char tmpname[64];
static FILE* p_f = NULL;
static aw_thread_struct_t thread_struct;
static struct timeval timeRef;
static long long t1;
static long long t2;
static float totTime;
static char cmd[1024];
static int err;
static int haveMp3Transcoder = 1;
static char username[32];
static char home[128];

const unsigned char RIFF[4] = {'R','I','F','F'};
const unsigned char WAVE[4] = {'W','A','V','E'};
const unsigned char FMT_CHUNK_MARKER[4] = {'f','m','t',' '};
const unsigned char DATA_CHUNK_HEADER[4] = {'d','a','t','a'};

typedef struct WaveHeader {

    unsigned char riff[4];
    uint32_t overall_size;
    unsigned char wave[4];
    unsigned char fmt_chunk_marker[4];
    uint32_t length_of_fmt;
    uint16_t format_type;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    unsigned char data_chunk_header[4];
    uint32_t data_size;

} WaveHeader;

typedef struct Gui {

    GtkWidget* main;

    GtkButtonBox* deviceOptionsButtonBox;

    GtkToggleButton* pcmPlugHwButton;
    gulong pcmPlugHwButtonId;

    GtkButtonBox* pcmNchannelsOptions;
    GtkButtonBox* pcmFramerateOptions;
    GtkButtonBox* pcmFormatOptions;

    GtkButton* recordstopButton;
    GtkButton* pauseButton;

    GtkBox* vuGraphicMetersBox;
    GtkButtonBox* vuNumericMetersBox;
    GtkBox* vuLabelsBox;

    GtkLevelBar* vuGraphicMeters[MAX_CHANNELS]; // TODO: better with malloc
    GtkButton* vuNumericMeters[MAX_CHANNELS]; // TODO: better with malloc

    GtkLabel* timeLabel;

} Gui;

Gui* GUI;


/*============================================================================
				functions and gui controllers
============================================================================*/


int arQuit_ ()
{   
    gtk_main_quit ();
}

int arQuit (GtkWidget* widget)
{   
    arQuit_ ();
}

int arOpenTempFile ()
{
    static time_t t;
    struct tm* timeinfo;
    char tmppath[512];

    time (&t);
    timeinfo = localtime (&t);
    strftime (tmpname, sizeof tmpname, "rec-%Y-%m-%d-%H-%M-%S", timeinfo);
    snprintf (tmppath, sizeof tmppath, "%s/.alsarecorder/tmp/%s", home, tmpname);
    
    if ((p_f = fopen (tmppath, "wb")) == NULL)
        return -1;

    return 0;
}

int arCloseTempFile ()
{    
    if ((fclose (p_f)) == EOF)
        return -1;    

    return 0;
}

int arSave ()
{   
    char recfolder[512];
    char tmppath[512];
    char wavpath[512];
    char mp3path[512];
    char userpath[512];
    FILE* p_tmpf;
    FILE* p_wavf;
    long dataSize;
    long fileSize;
    char buffer[1024];
    uint32_t bytesNum = 0;
    const unsigned int HEADERS_SIZE = 44;
    WaveHeader wh;
    GtkWidget* dialog;
    GtkFileChooser* chooser;
    gint response;
    
    dialog = gtk_file_chooser_dialog_new ("Save Recording",
                                          NULL,
                                          GTK_FILE_CHOOSER_ACTION_SAVE,
                                          "_Cancel",
                                          GTK_RESPONSE_CANCEL,
                                          "_Save",
                                          GTK_RESPONSE_ACCEPT,
                                          NULL);
    
    chooser = GTK_FILE_CHOOSER (dialog);

    gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);

    // get paths
    snprintf (tmppath, sizeof tmppath, "%s/.alsarecorder/tmp/%s", home, tmpname);
    snprintf (recfolder, sizeof recfolder, "%s/Recordings", home);

    if (saveFormat == SAVE_TO_WAV)
    {
        snprintf (wavpath, sizeof wavpath, "%s.wav", tmpname);
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), wavpath);

    } else {

        snprintf (wavpath, sizeof wavpath, "%s/.alsarecorder/tmp/%s.wav", home, tmpname);
        snprintf (mp3path, sizeof mp3path, "%s.mp3", tmpname);
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), mp3path);
    }
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), recfolder);

    response = gtk_dialog_run (GTK_DIALOG (dialog));

    if (response == GTK_RESPONSE_ACCEPT)
    {
        snprintf (userpath, sizeof userpath, "%s", gtk_file_chooser_get_filename (chooser));
        gtk_widget_destroy (dialog);
        
    } else {

        if (remove (tmppath) != 0)
            printf ("Error in removing tmp recordings.");
        gtk_widget_destroy (dialog);

        return -1;
    }        
    
    // open
    if ((p_tmpf = fopen (tmppath, "rb")) == NULL)
        return -1;

    if (saveFormat == SAVE_TO_WAV)
    {
        if ((p_wavf = fopen (userpath, "wb")) == NULL)
            return -1;

    } else {

        if ((p_wavf = fopen (wavpath, "wb")) == NULL)
            return -1;
    }

    fseek (p_tmpf, 0L, SEEK_END);
    dataSize = ftell (p_tmpf);
    fseek (p_tmpf, 0L, SEEK_SET);

    fileSize = dataSize + HEADERS_SIZE;

    memcpy (wh.riff, RIFF, 4);
    wh.overall_size = fileSize - 8;
    memcpy (wh.wave, WAVE, 4);
    memcpy (wh.fmt_chunk_marker, FMT_CHUNK_MARKER, 4);
    wh.length_of_fmt = 16;
    wh.format_type = 1;
    wh.channels = aw_pcm_params.nchannels;
    wh.sample_rate = aw_pcm_params.framerate;
    wh.byterate = aw_pcm_params.byterate;
    wh.block_align = aw_pcm_params.framesize;
    wh.bits_per_sample = aw_pcm_params.real_bits;
    memcpy (wh.data_chunk_header, DATA_CHUNK_HEADER, 4);
    wh.data_size = dataSize;

    fwrite (&wh, sizeof wh, 1, p_wavf);

    // read and write rest    
    while (bytesNum >= 0 && !feof (p_tmpf))
    {
        bytesNum = fread (buffer, 1, 1024, p_tmpf);
        fwrite (buffer, 1, bytesNum, p_wavf);
    }

    // close
    if ((fclose (p_tmpf)) == EOF)
        return -1;
    if ((fclose (p_wavf)) == EOF)
        return -1;

    // remove tmp file  
    if (remove (tmppath) != 0)
        return -1;

    // transcode in mp3
    if (saveFormat == SAVE_TO_MP3)
    {
        if (system (NULL))
        {
            snprintf (cmd, sizeof cmd, "ffmpeg -i \"%s\" -codec:a libmp3lame -qscale:a 2 \"%s\"", wavpath, userpath);
            if (system (cmd) != 0)
                printf ("error in mp3 transcoding");
        }
        // remove tmp file  
        if (remove (wavpath) != 0)
            return -1;
    }
    return 0;
}

int arRecordStop_ ()
{
    if (state == AW_RECORDING || state == AW_PAUSED)
    {
        state = AW_MONITORING;
        
        gtk_button_set_image (GUI->recordstopButton, gtk_image_new_from_file ("media/record_noactive.png"));
        gtk_button_set_image (GUI->pauseButton, gtk_image_new_from_file ("media/pause_noactive.png"));
        
        arCloseTempFile ();        
        arSave ();

        gtk_label_set_text (GUI->timeLabel, "0.00");

        gtk_widget_set_sensitive (GTK_WIDGET (GUI->deviceOptionsButtonBox), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmNchannelsOptions), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFramerateOptions), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFormatOptions), TRUE);

    } else if (state == AW_MONITORING) {

        if (arOpenTempFile () < 0) {

            printf ("error in open tmp recording file\n\n");
            return -1;
        }
        
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->deviceOptionsButtonBox), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmNchannelsOptions), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFramerateOptions), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFormatOptions), FALSE);
        
        gtk_button_set_image (GUI->recordstopButton, gtk_image_new_from_file ("media/record_active.png"));
        
        state = AW_RECORDING;
        
        gettimeofday (&timeRef, NULL);
        totTime = 0;
        t1 = (long long) timeRef.tv_sec * 1000L + timeRef.tv_usec / 1000;
    }    
}

int arRecordStop (GtkButton* button)
{
    arRecordStop_ ();
}

int arPause (GtkButton* button)
{
    if (state == AW_PAUSED)
    {
        state = AW_RECORDING;
        gtk_button_set_image (GUI->pauseButton, gtk_image_new_from_file ("media/pause_noactive.png"));

        gettimeofday (&timeRef, NULL);
        totTime = totTime + ((float) (t2 - t1)) / 1000.0;
        t1 = (long long) timeRef.tv_sec * 1000L + timeRef.tv_usec / 1000;

    } else if (state == AW_RECORDING) {

        gtk_button_set_image (GUI->pauseButton, gtk_image_new_from_file ("media/pause_active.png"));
        state = AW_PAUSED;
    }   
}

gboolean arUpdateStatsAndVUMeters (gpointer data)
{
    int i;
    char value[16];
    float level;
    float time;
    unsigned int int_part;
    unsigned int dec_part;

    for (i = 0; i < aw_pcm_params.nchannels; i++)
    {
        if (vuFormat == VU_LOGARITHMIC)
        {
            level = ss.avg_log[i];

        } else {
            
            level = ss.avg_power[i];
        }        
        if (level == 0) level = 1;
        
        gtk_level_bar_set_value (GUI->vuGraphicMeters[i], level);
        
        if (ss.clip[i])
        {
            gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (GUI->vuNumericMeters[i])), "vu-numeric-meter-clipped");
            gtk_button_set_label (GUI->vuNumericMeters[i], "clip");

        } else {

            snprintf (value, sizeof value, "%d", (uint8_t) ss.max[i]);
            gtk_button_set_label (GUI->vuNumericMeters[i], value);
        }        
    }

    if (state == AW_RECORDING)
    {
        gettimeofday (&timeRef, NULL);
        t2 = (long long) timeRef.tv_sec * 1000L + timeRef.tv_usec / 1000;
        
        time = totTime + ((float) (t2 - t1)) / 1000.0;
        int_part = (unsigned int) time;
        dec_part = (unsigned int) ((time - int_part) * 100);

        snprintf (value, sizeof value, "%d.%02d", int_part, dec_part);

        gtk_label_set_text (GUI->timeLabel, value);
        
        if (totTime > MAX_TOT_TIME) arRecordStop_();
    }

    // check if continue callback
    if (state == AW_MONITORING || state == AW_RECORDING || state == AW_PAUSED)
    {
        return TRUE;

    } else {

        return FALSE;
    }
}

int arAbout ()
{
    GtkWidget *dialog, *label, *content;

    dialog = gtk_dialog_new_with_buttons ("About", GTK_WINDOW (GUI->main), GTK_DIALOG_DESTROY_WITH_PARENT, "_OK", GTK_RESPONSE_NONE, NULL);

    content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    label = gtk_label_new ("ALSA recorder\n"
        "\n"
        "Copyright (c) 2021 Fabio Michelini (github)\n"
        "\n"
        "Permission to use, copy, modify, and/or distribute this software for any\n"
        "purpose with or without fee is hereby granted, provided that the above\n"
        "copyright notice and this permission notice appear in all copies.\n"
        "\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\" AND THE AUTHOR DISCLAIMS ALL WARRANTIES\n"
        "WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF\n"
        "MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY\n"
        "SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER\n"
        "RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF\n"
        "CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN\n"
        "CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.");
    
    g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);

    gtk_style_context_add_class (gtk_widget_get_style_context (content), "about-content");
    gtk_container_add (GTK_CONTAINER (content), label);
    gtk_widget_show_all (dialog);
}

int arGetPcmDevices ()
{
    int i;
    int j;
    uint8_t all_aw_pcms_length;
    AwPcm all_aw_pcms[AW_MAX_PCMS_LENGTH];

    aw_get_pcm_devices (all_aw_pcms, &all_aw_pcms_length);
    
    aw_pcms_length = 0;
    for (i = 0; i < all_aw_pcms_length; i++)
    {
        if (all_aw_pcms[i].mode == SND_PCM_STREAM_CAPTURE)
        {
            aw_pcms[aw_pcms_length] = all_aw_pcms[i];
            aw_pcms_length++;
        }
    }
    return 0;
}

int arGetPcmDevice (AwPcm** p_p_aw_pcm)
{    
    *p_p_aw_pcm = &aw_pcms[0];
    return 0;
}

int arResetNumericMeter (GtkButton* button)
{
    int i = atoi (gtk_widget_get_name ( GTK_WIDGET (button)));

    gtk_style_context_remove_class (gtk_widget_get_style_context (GTK_WIDGET (button)), "vu-numeric-meter-clipped");
    gtk_button_set_label (button, "0");
    ss.clip[i] = 0; // TODO
    ss.max[i] = 0; // TODO
}

int arDrawVUMeters ()
{
    GList* children;
    GList* iter;
    GtkLevelBar* bar;
    GtkButton* button;
    GtkLabel* label;
    int i;
    char ch[8];    
    char val[8];    
    
    /* clean panel */

    children = gtk_container_get_children (GTK_CONTAINER (GUI->vuGraphicMetersBox));
    
    for (iter=children; iter != NULL; iter = g_list_next (iter))
        gtk_widget_destroy (GTK_WIDGET (iter->data));
        
    g_list_free (children);

    children = gtk_container_get_children (GTK_CONTAINER (GUI->vuNumericMetersBox));
    
    for (iter=children; iter != NULL; iter = g_list_next (iter))
        gtk_widget_destroy (GTK_WIDGET (iter->data));

    g_list_free (children);

    children = gtk_container_get_children (GTK_CONTAINER (GUI->vuLabelsBox));

    for (iter=children; iter != NULL; iter = g_list_next (iter)) 
        gtk_widget_destroy (GTK_WIDGET (iter->data));

    g_list_free (children);

    /* draw vu meters (graphic, buttons, labels) */  
    
    for (i = 0; i < aw_pcm_params.nchannels; i++)
    {
        snprintf (val, sizeof val, "%d", i);
        snprintf (ch, sizeof ch, "ch %d", i);

        /* graphic */
        bar = GTK_LEVEL_BAR (gtk_level_bar_new_for_interval (0, 100));
        gtk_level_bar_set_value (bar, 70);
        gtk_level_bar_set_inverted (bar, TRUE);
        gtk_orientable_set_orientation (GTK_ORIENTABLE (bar), GTK_ORIENTATION_VERTICAL);
        gtk_level_bar_set_mode (bar, GTK_LEVEL_BAR_MODE_DISCRETE);
        gtk_box_pack_start (GUI->vuGraphicMetersBox, GTK_WIDGET (bar), TRUE, TRUE, 2);
        GUI->vuGraphicMeters[i] = bar;
        
        /* numeric button */
        button = GTK_BUTTON (gtk_button_new_with_label ("0"));
        gtk_widget_set_name (GTK_WIDGET (button), val);
        gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (button)), "vu-numeric-meter");
        g_signal_connect (GTK_WIDGET (button), "clicked", G_CALLBACK (arResetNumericMeter), NULL);
        gtk_box_pack_start (GTK_BOX (GUI->vuNumericMetersBox), GTK_WIDGET (button), TRUE, TRUE, 2);
        GUI->vuNumericMeters[i] = button;

        /* label */        
        label =  GTK_LABEL (gtk_label_new (ch));
        gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (label)), "vu-label");
        gtk_box_pack_start (GUI->vuLabelsBox, GTK_WIDGET (label), TRUE, TRUE, 2);        
    }
    gtk_widget_show_all (GTK_WIDGET (GUI->vuGraphicMetersBox));
    gtk_widget_show_all (GTK_WIDGET (GUI->vuNumericMetersBox));
    gtk_widget_show_all (GTK_WIDGET (GUI->vuLabelsBox));
}

int arPcmStart ()
{      
    pthread_t thread_id;
    char name[sizeof (*p_aw_pcm).name + sizeof "plug"];
    
    /* open and set pcm */
    
    if (defaultPcm)
    {
        snprintf (name, sizeof name, "plug%s", (*p_aw_pcm).name);
            
    } else {

        snprintf (name, sizeof name, "%s", (*p_aw_pcm).name);
    }
    if ((err = snd_pcm_open (&p_pcm, name, SND_PCM_STREAM_CAPTURE, SND_PCM_ASYNC)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    if ((err = aw_set_params (p_pcm, &aw_pcm_params)) < 0)
        return aw_handle_err ("cannot set params");  
    
    aw_print_params (aw_pcm_params);

    if ((err = snd_pcm_prepare (p_pcm)) < 0)
        return aw_handle_err (snd_strerror (err));
        
    if ((err = snd_pcm_state (p_pcm)) != SND_PCM_STATE_PREPARED)
        return aw_handle_err( snd_strerror (err));
    
    if ((err = snd_pcm_start (p_pcm)) < 0)
        return aw_handle_err (snd_strerror (err));
    
    state = AW_MONITORING;
            
    /* prepare stats struct */

    aw_build_compute_struct (aw_pcm_params, &ss);   
    
    /* start thread */

    thread_struct.p_pcm = p_pcm;
    thread_struct.p_hw_params = &aw_pcm_params;
    thread_struct.p_p_f = &p_f;
    thread_struct.p_ss = &ss;
    thread_struct.p_state = &state; 
    
    pthread_create (&thread_id, NULL, aw_thread_func, (void*) &thread_struct); 
    
    sleep (0.5);
    
    g_timeout_add (UPDATE_TIMER_INTERVAL, (GSourceFunc) arUpdateStatsAndVUMeters, NULL);    
    
    return 0;
}

int arPcmStop ()
{      
    state = AW_STOPPING;

    while (state != AW_STOPPED) sleep (0.1);    
    
    /* free stats struct */

    aw_free_compute_struct (&ss);       

    if ((err = snd_pcm_close (p_pcm)) < 0)
        return aw_handle_err (snd_strerror(err));

    return 0;
}

int arChangePcmOptionsNChannels (GtkRadioButton* button) {

    uint8_t old_nchannels;
    int i;

    if (state == AW_RECORDING || state == AW_PAUSED) return 0;

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    {
        i = atoi (gtk_widget_get_name (GTK_WIDGET (button)));
        
        if (aw_pcm_params.nchannels != (*p_aw_pcm).nchannels[i])
        {
            // TODO: freeze gui with restart icon
            arPcmStop ();
            old_nchannels = aw_pcm_params.nchannels;
            aw_pcm_params.nchannels = (*p_aw_pcm).nchannels[i];       
            if (arPcmStart () != 0){
                aw_pcm_params.nchannels = old_nchannels;
                if (arPcmStart () != 0)
                    arQuit_ ();
                // TODO: reset old button
            }
            // TODO: unfreeze gui with restart icon
        }
    }
    arDrawVUMeters ();
}

int arChangePcmOptionsFramerate (GtkRadioButton* button) {
    
    uint32_t old_framerate;
    int i;

    if (state == AW_RECORDING || state == AW_PAUSED) return 0;

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    {
        i = atoi (gtk_widget_get_name (GTK_WIDGET (button)));
        
        if (aw_pcm_params.framerate != (*p_aw_pcm).framerates[i])
        {
            // TODO: freeze gui with restart icon
            arPcmStop ();
            old_framerate = aw_pcm_params.framerate;
            aw_pcm_params.framerate = (*p_aw_pcm).framerates[i];     
            if (arPcmStart () != 0)
            {
                aw_pcm_params.framerate = old_framerate;
                if (arPcmStart () != 0)
                    arQuit_ ();
                // TODO: reset old button
            }
            // TODO: unfreeze gui with restart icon
        }
    
    }
}

int arChangePcmOptionsFormat (GtkRadioButton* button) {
    
    snd_pcm_format_t old_format;
    int i;

    if (state == AW_RECORDING || state == AW_PAUSED) return 0;

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    {
        i = atoi (gtk_widget_get_name (GTK_WIDGET (button)));
        
        if (aw_pcm_params.format != (*p_aw_pcm).formats[i])
        {
            // TODO: freeze gui with restart icon
            arPcmStop ();
            old_format = aw_pcm_params.format;
            aw_pcm_params.format = (*p_aw_pcm).formats[i];      
            if (arPcmStart () != 0){
                aw_pcm_params.format = old_format;
                if (arPcmStart () != 0)
                    arQuit_ ();
                // TODO: reset old button
            }
            // TODO: unfreeze gui with restart icon
        }
    }
}

int arSwitchDefaultPcm (GtkToggleButton* button)
{
    arPcmStop ();

    if (gtk_toggle_button_get_active (button))
    {
        defaultPcm = 1;

        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmNchannelsOptions), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFramerateOptions), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFormatOptions), FALSE);

        _nchannels = aw_pcm_params.nchannels;
        _framerate = aw_pcm_params.framerate;
        _format = aw_pcm_params.format;

        aw_pcm_params.nchannels = AW_DEFAULT_NCHANNELS;
        aw_pcm_params.framerate = AW_DEFAULT_FRAMERATE;
        aw_pcm_params.format = AW_DEFAULT_FORMAT;       

        arDrawVUMeters ();        

        if (arPcmStart () != 0)
            arQuit_ ();
            
    } else {
        
        defaultPcm = 0;

        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmNchannelsOptions), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFramerateOptions), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFormatOptions), TRUE);  

        aw_pcm_params.nchannels = _nchannels;
        aw_pcm_params.framerate = _framerate;
        aw_pcm_params.format = _format;
        
        arDrawVUMeters ();   
        
        if (arPcmStart () != 0)
            arQuit_ ();              
    }    
}

int arUpdatePcmOptionsPanel ()
{
    GList* children;
    GList* iter;
    GSList* group;
    GtkRadioButton* button;
    int i;
    char label[64];
    char name[8];    

    /* clean panel */
    children = gtk_container_get_children (GTK_CONTAINER (GUI->pcmNchannelsOptions));
    
    for (iter=children; iter != NULL; iter = g_list_next (iter))
        gtk_widget_destroy (GTK_WIDGET (iter->data));
        
    g_list_free (children);

    children = gtk_container_get_children (GTK_CONTAINER (GUI->pcmFramerateOptions));
    
    for (iter=children; iter != NULL; iter = g_list_next (iter))
        gtk_widget_destroy (GTK_WIDGET (iter->data));

    g_list_free (children);

    children = gtk_container_get_children (GTK_CONTAINER (GUI->pcmFormatOptions));

    for (iter=children; iter != NULL; iter = g_list_next (iter)) 
        gtk_widget_destroy (GTK_WIDGET (iter->data));

    g_list_free (children);

    /* draw nchannels radio box */  
    
    aw_pcm_params.nchannels = (*p_aw_pcm).nchannels[0];
    group = NULL;
    
    for (i = 0; i < AW_MAX_NCHANNELS_LENGTH; i++)
    {
        if ((*p_aw_pcm).nchannels[i] == -1) break;

        snprintf (label, sizeof label, "%d", (*p_aw_pcm).nchannels[i]);        
        snprintf (name, sizeof name, "%d", i);        
        button = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group, label));
        group = gtk_radio_button_get_group (button);
        gtk_widget_set_name (GTK_WIDGET (button), name);
        gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (button), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);

        gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (button)), "option-button");
        
        if ((*p_aw_pcm).nchannels[i] == AW_DEFAULT_NCHANNELS)
        {                  
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
            aw_pcm_params.nchannels = AW_DEFAULT_NCHANNELS;
        }
        g_signal_connect(GTK_WIDGET (button), "clicked", G_CALLBACK (arChangePcmOptionsNChannels), NULL);
        gtk_container_add (GTK_CONTAINER (GUI->pcmNchannelsOptions), GTK_WIDGET (button));
    }
    gtk_widget_show_all (GTK_WIDGET (GUI->pcmNchannelsOptions));

    /* draw framerates radio box */   

    aw_pcm_params.framerate = (*p_aw_pcm).framerates[0];
    
    group = NULL;
    
    for (i = 0; i < AW_MAX_FRAMERATES_LENGTH; i++)
    {
        if ((*p_aw_pcm).framerates[i] == -1) break;
        
        snprintf (label, sizeof label, "%d", (*p_aw_pcm).framerates[i]);   
        snprintf (name, sizeof name, "%d", i);        
        button = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group, label));
        group = gtk_radio_button_get_group (button);
        gtk_widget_set_name (GTK_WIDGET (button), name);
        gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (button), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);

        gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (button)), "option-button");
        
        if ((*p_aw_pcm).framerates[i] == AW_DEFAULT_FRAMERATE)
        {
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
            aw_pcm_params.framerate = AW_DEFAULT_FRAMERATE;
        }
        g_signal_connect (GTK_WIDGET (button), "clicked", G_CALLBACK (arChangePcmOptionsFramerate), NULL);
        gtk_container_add (GTK_CONTAINER (GUI->pcmFramerateOptions), GTK_WIDGET (button));
    }
    gtk_widget_show_all (GTK_WIDGET (GUI->pcmFramerateOptions));

    /* draw formats radio box */   

    aw_pcm_params.format = (*p_aw_pcm).formats[0];
    group = NULL;
    
    for (i = 0; i < AW_MAX_FORMATS_LENGTH; i++)
    {
        if ((*p_aw_pcm).formats[i] == -1) break;
        
        snprintf (label, sizeof label, "%s", snd_pcm_format_name ((*p_aw_pcm).formats[i]));   
        snprintf (name, sizeof name, "%d", i);
        button = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group, label));
        group = gtk_radio_button_get_group (button);
        gtk_widget_set_name (GTK_WIDGET (button), name);
        gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (button), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);
        gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (button)), "option-button");
        
        if ((*p_aw_pcm).formats[i] == AW_DEFAULT_FORMAT)
        {                  
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
            aw_pcm_params.format = AW_DEFAULT_FORMAT;

        }
        g_signal_connect(GTK_WIDGET (button), "clicked", G_CALLBACK (arChangePcmOptionsFormat), NULL);
        gtk_container_add (GTK_CONTAINER (GUI->pcmFormatOptions), GTK_WIDGET (button));
    }
    gtk_widget_show_all (GTK_WIDGET (GUI->pcmFormatOptions));
    
    /* plughw */

    snprintf (label, sizeof label, "cd quality (plug%s)", (*p_aw_pcm).name);   
    gtk_button_set_label (GTK_BUTTON (GUI->pcmPlugHwButton), label);
    
    if (GUI->pcmPlugHwButtonId > 0){
        g_signal_handler_disconnect (GTK_WIDGET (GUI->pcmPlugHwButton), GUI->pcmPlugHwButtonId);}

    if ((*p_aw_pcm).has_plughw)
    {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GUI->pcmPlugHwButton), TRUE);
    
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmNchannelsOptions), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFramerateOptions), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFormatOptions), FALSE);
        
        _nchannels = aw_pcm_params.nchannels;
        _framerate = aw_pcm_params.framerate;
        _format = aw_pcm_params.format;

        aw_pcm_params.nchannels = AW_DEFAULT_NCHANNELS;
        aw_pcm_params.framerate = AW_DEFAULT_FRAMERATE;
        aw_pcm_params.format = AW_DEFAULT_FORMAT;  
        
        defaultPcm = 1;     
     
    } else {
        
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GUI->pcmPlugHwButton), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmPlugHwButton), FALSE);

        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmNchannelsOptions), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFramerateOptions), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (GUI->pcmFormatOptions), TRUE);        

        defaultPcm = 0;
    }        
    GUI->pcmPlugHwButtonId = g_signal_connect (GTK_WIDGET (GUI->pcmPlugHwButton), "toggled", G_CALLBACK (arSwitchDefaultPcm), NULL);
    
    arDrawVUMeters ();   
}

int arChangePcmDevice (GtkRadioButton* button)
{    
    int i;
    
    if (state == AW_RECORDING || state == AW_PAUSED) return 0;
    
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    {
        for (i = 0; i < aw_pcms_length; i++)
        {            
            if (strcmp (gtk_widget_get_name (GTK_WIDGET (button)), aw_pcms[i].name) == 0)
            {
                if (&aw_pcms[i] != p_aw_pcm)
                {
                    // TODO: freeze gui with restart icon
                    arPcmStop ();
                    p_aw_pcm = &aw_pcms[i];                    
                    arUpdatePcmOptionsPanel ();   
                    
                    aw_print_pcm (p_aw_pcm);

                    if (arPcmStart () != 0) return -1;
                    // TODO: unfreeze gui with restart icon
                }
                break;
            }
        }
    }
}

int arDrawPcmDevicesPanel ()
{   
    int i;
    GtkRadioButton* button;
    GSList* group = NULL;
    char cardname[32];
    char label[64];
    
    for (i = 0; i < aw_pcms_length; i++)
    {
        strncpy (cardname, aw_pcms[i].cardname, sizeof cardname);
        snprintf (label, sizeof label, "%s (%s)", cardname, aw_pcms[i].name);
        button = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group, label));
        group = gtk_radio_button_get_group (button);
        gtk_container_add (GTK_CONTAINER (GUI->deviceOptionsButtonBox), GTK_WIDGET (button));
        gtk_widget_set_name (GTK_WIDGET (button), aw_pcms[i].name);

        if (aw_pcms[i].name == (*p_aw_pcm).name)
        {
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
        }
        g_signal_connect(GTK_WIDGET (button), "clicked", G_CALLBACK (arChangePcmDevice), NULL);
    }
    gtk_widget_show_all (GTK_WIDGET (GUI->deviceOptionsButtonBox));
}

int arSwitchVUFormat (GtkButton* button)
{
    if (vuFormat == VU_LOGARITHMIC)
    {
        vuFormat = VU_NORMAL;
        gtk_button_set_label (button, "normal VU meters");

    } else {

        vuFormat = VU_LOGARITHMIC;
        gtk_button_set_label (button, "logarithmic VU meters");
    }
}

int arSwitchSaveFormat (GtkRadioButton* button)
{
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    {
        if (strcmp (gtk_widget_get_name (GTK_WIDGET (button)), "mp3") == 0)
        {
            saveFormat = SAVE_TO_MP3;

        } else {

            saveFormat = SAVE_TO_WAV;
        }
    }
}


/*============================================================================
				main cycle
============================================================================*/


int main (int argc, char **argv)
{
    GError* err = NULL;
    GtkBuilder* builder; 
    GtkWidget* window;
    GdkPixbuf* icon;    
    GdkDisplay* display;
    GdkScreen* screen;
    GtkCssProvider* provider;

    struct passwd *pw = getpwuid (getuid ());

    
    /*==============================
        some test and deplayments
    ==============================*/    


    snprintf (home, sizeof home, "%s", pw->pw_dir);
    getlogin_r (username, sizeof username);
    if (username == NULL || home == NULL) return EXIT_FAILURE;

    // ensure resources    
    snprintf (cmd, sizeof cmd, "mkdir -p %s/.alsarecorder/tmp", home);
    if (system(cmd) != 0)
    {
        printf("error create .alsarecorder dir\n");
        return -1;
    }
    snprintf (cmd, sizeof cmd, "mkdir -p %s/Recordings", home);
    if (system(cmd) != 0)
    {
        printf("error create Recordings dir\n");
        return -1;
    }
    if (system("ffmpeg -version") != 0)
    {
        haveMp3Transcoder = 0;
        printf("no ffmpeg available: disable mp3 transcode\n");
    }


    /*=============
        backend
    ===============*/


    arGetPcmDevices ();

    aw_print_pcms (aw_pcms, aw_pcms_length);
    
    if (arGetPcmDevice (&p_aw_pcm) < 0)
        return -1;
        
    aw_print_pcm (p_aw_pcm);
    

    /*=============
        gui
    ===============*/


    // gchar const *text = "<span font='32' weight='bold' color='#DDDDDD'>Enter the Secret\nCode. ('1')</span>";
    // gtk_label_set_markup (GTK_LABEL (window), text);

    gtk_init (&argc, &argv);

    /* load gui from glade */

    builder = gtk_builder_new ();
    gtk_builder_add_from_file (builder, "alsarecorder.glade", &err);

    if (err != NULL) {
        fprintf (stderr, "Unable to read file: %s\n", err->message);
        g_error_free (err);
        return 1;
    }

    window = GTK_WIDGET (gtk_builder_get_object (builder, "main"));

    if (window == NULL || !GTK_IS_WINDOW (window)) {
        fprintf (stderr, "Unable to get window. (window == NULL || window != GtkWindow)\n");
        return 1;
    }

    /* store gui in struct */

    GUI = malloc(sizeof (Gui));
    
    GUI->main = window;
    GUI->deviceOptionsButtonBox = GTK_BUTTON_BOX (gtk_builder_get_object (builder, "device-options"));
    GUI->pcmPlugHwButton = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "pcm-plughw-toggle-button"));
    GUI->pcmPlugHwButtonId = 0;
    GUI->pcmNchannelsOptions = GTK_BUTTON_BOX (gtk_builder_get_object (builder, "pcm-nchannels-options"));
    GUI->pcmFramerateOptions = GTK_BUTTON_BOX (gtk_builder_get_object (builder, "pcm-framerate-options"));
    GUI->pcmFormatOptions = GTK_BUTTON_BOX (gtk_builder_get_object (builder, "pcm-format-options"));
    GUI->recordstopButton = GTK_BUTTON (gtk_builder_get_object (builder, "recordstop-button"));
    GUI->pauseButton = GTK_BUTTON (gtk_builder_get_object (builder, "pause-button"));
    GUI->timeLabel = GTK_LABEL (gtk_builder_get_object (builder, "time-label"));
    GUI->vuGraphicMetersBox = GTK_BOX (gtk_builder_get_object (builder, "vu-graphic-meters"));
    GUI->vuNumericMetersBox = GTK_BUTTON_BOX (gtk_builder_get_object (builder, "vu-numeric-meters"));
    GUI->vuLabelsBox = GTK_BOX (gtk_builder_get_object (builder, "vu-labels"));
    
    /* device options */

    arDrawPcmDevicesPanel ();
    arUpdatePcmOptionsPanel ();

    /* signals */   

    gtk_builder_connect_signals (builder, window);    
    g_object_unref (builder);

    /* style */

    provider = gtk_css_provider_new ();
    display = gdk_display_get_default ();
    screen = gdk_display_get_default_screen (display);

    gtk_style_context_add_provider_for_screen (screen, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_css_provider_load_from_path (provider, "alsarecorder.css", NULL);

    g_object_unref (provider);

    /* main cycle */

    gtk_label_set_text (GUI->timeLabel, "0.00");

    gtk_widget_show_all (window);
    if (arPcmStart () != 0)
    {
        printf ("pcm not working");
        return -1;

    } else {

        gtk_main ();
    }

    free (GUI);

    return 0;
}