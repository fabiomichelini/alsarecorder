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
 * apt install libasound2-dev
 * 
 *
 * compile with: g++ `wx-config --cxxflags` alsarecorder.cpp alsawrapper.c -lasound -lpthread -lm `wx-config --libs` -o alsarecorder
*/

#include "sys/time.h"
#include "time.h"
#include "signal.h"
#include "stdlib.h"

#include "wx/wx.h"
#include "wx/tglbtn.h"
#include "wx/button.h"
#include "wx/statline.h"
#include "wx/timer.h"

#include "alsawrapper.h"    


/*============================================================================
                header declarations
============================================================================*/


static AwPcm* p_aw_pcm = NULL;
static snd_pcm_t* p_pcm = NULL;
static AwPcm aw_pcms[AW_MAX_PCMS_LENGTH];
static unsigned int aw_pcms_length;
static AwPcmParams aw_pcm_params;
static aw_record_state_t state;
static const char* saveFormat;
static bool logarithmic;
static AwComputeStruct ss;
static char tmpname[64];
static FILE* p_f;
static aw_thread_struct_t thread_struct;
static const unsigned int UPDATE_TIMER_INTERVAL = 100;
static timeval timeRef;
static long long t1;
static long long t2;
static float totTime;
static const float MAX_TOT_TIME = 3600; // in sec
static char* username = getenv("USER");
static char cmd[1024];
static int err;
static bool haveMp3Transcoder = true;


enum {

    SAVE_TO_WAV = 0,
    SAVE_TO_MP3 = 1
};

enum {

    METER_NORMAL = 0,
    METER_LOGARITHMIC = 0
};

typedef struct WaveHeader {

    const unsigned char riff[4] = {'R','I','F','F'};
    uint32_t overall_size;
    const unsigned char wave[4] = {'W','A','V','E'};
    const unsigned char fmt_chunk_marker[4] = {'f','m','t',' '};
    uint32_t length_of_fmt;
    uint16_t format_type;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    const unsigned char data_chunk_header[4] = {'d','a','t','a'};
    uint32_t data_size;

} WaveHeader;


class ARTopFrame : public wxFrame {

public:

    ARTopFrame (wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos=wxDefaultPosition, const wxSize& size=wxDefaultSize, long style=wxDEFAULT_FRAME_STYLE);

    // void ARTopFrame::intHandler ();

private:

    void about (wxCommandEvent& event);
    void save (wxCommandEvent& event);
    // void reset (wxCommandEvent& event);
    void quit (wxCommandEvent& event);

    void recordstop (wxCommandEvent& event);
    void pause (wxCommandEvent& event);

    void test ();

    int updatePcmDevices ();
    void changePcmDevice (wxCommandEvent& event);
    void changePcmOptions (wxCommandEvent& event);
    void changePcmOptionsNChannels (wxCommandEvent& event);
    void changePcmOptionsFramerate (wxCommandEvent& event);
    void changePcmOptionsFormat (wxCommandEvent& event);
    int getPcmDevice (AwPcm** p_p_pcm);
    void drawPcmDevicesPanel ();
    void updatePcmOptionsPanel ();
    void updateStatsAndVUMeters (wxTimerEvent& event);
    void resetClip (wxCommandEvent& event);
    int pcmStop ();
    int pcmStart ();
    void recordstop_ ();
    int save_ ();
    int openTempFile ();
    int closeTempFile ();
    int copyTempToRecordingFile();

    wxPanel* pcmDevicesPanel;
    wxPanel* pcmOptionsPanel;
    wxRadioBox* pcmOptionsNChannels;
    wxRadioBox* pcmOptionsFormats;
    wxRadioBox* pcmOptionsFramerates;
    wxRadioBox* saveFormatRadioBox;
    wxToggleButton* logarithmicToggleButton;
    wxStaticText* timerLabel;
    wxBitmap* imageRecordNoactive;
    wxBitmap* imageRecordActive;
    wxBitmap* imagePauseNoactive;
    wxBitmap* imagePauseActive;
    wxBitmapButton* recordstopButton;
    wxBitmapButton* pauseButton;    
    // wxGauge* vuMeters[AW_MAX_NCHANNELS_LENGTH];
    wxPanel* vuContainer;
    wxPanel* vuMeters[AW_MAX_NCHANNELS_LENGTH];
    wxButton* clipsButtons[AW_MAX_NCHANNELS_LENGTH];
    wxStaticText* clipsLabels[AW_MAX_NCHANNELS_LENGTH];
    wxMenuBar* menuBar;

    wxTimer   updateTimer;
};


/*============================================================================
				gui and program
============================================================================*/


ARTopFrame::ARTopFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style):
    wxFrame(parent, id, title, pos, size, wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX))
{
    int i;
    char ch[32];    
    unsigned int ID_QUIT = wxNewId();
    unsigned int ID_ABOUT = wxNewId();

    /* init globals */

    state = AW_STOPPED;
    saveFormat = AW_DEFAULT_SAVE_FORMAT;
    logarithmic = false;

    updatePcmDevices ();

    if ((err = getPcmDevice (&p_aw_pcm)) < 0)
        Close(true);
        
    aw_print_pcm (p_aw_pcm);
    
    /* GUI */

    wxColour colour;
    wxIcon mainicon;

    mainicon.LoadFile("media/main.ico");
    this->SetIcon(mainicon);
    
    wxFont bodyFont (9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    wxFont titleFont (9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    wxFont logFont (10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    wxFont timerFont (24, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);

    this->SetFont (bodyFont);

    /* left panel */
    wxPanel* leftPanel = new wxPanel (this, wxID_ANY);

    /* pcm device panel */
    pcmDevicesPanel = new wxPanel (leftPanel, wxID_ANY);

    /* pcm device label */
    wxStaticText* pcmDevicesLabel = new wxStaticText (pcmDevicesPanel, wxID_ANY, "Device options", wxPoint (10, 15), wxSize (200, 20));
    pcmDevicesLabel->SetFont (titleFont);
    
    /* pcm device radiobox */
    drawPcmDevicesPanel ();
    
    /* pcm options panel */
    pcmOptionsPanel = new wxPanel (leftPanel, wxID_ANY);

    pcmOptionsNChannels = NULL;
    pcmOptionsFormats = NULL;
    pcmOptionsFramerates = NULL;
    
    updatePcmOptionsPanel ();     

    /* pcm options label and labels */
    wxStaticText* pcmOptionsLabel = new wxStaticText (pcmOptionsPanel, wxID_ANY, "Pcm Options", wxPoint (10, 30), wxSize (200, 20));
    pcmOptionsLabel->SetFont (titleFont);
    wxStaticText* pcmOptionsChannelsLabel = new wxStaticText (pcmOptionsPanel, wxID_ANY, "channels", wxPoint (14, 57), wxDefaultSize);
    wxStaticText* pcmOptionsFormatsLabel = new wxStaticText (pcmOptionsPanel, wxID_ANY, "framerate", wxPoint (14, 82), wxDefaultSize);
    wxStaticText* pcmOptionsRatesLabel = new wxStaticText (pcmOptionsPanel, wxID_ANY, "format", wxPoint (14, 107), wxDefaultSize);
        
    /* save format panel */
    wxPanel* saveFormatPanel = new wxPanel (leftPanel, wxID_ANY);

    /* save format label */
    wxStaticText* saveFormatLabel = new wxStaticText (saveFormatPanel, wxID_ANY, "Save Format", wxPoint (10, 30), wxSize (200, 20), 1);
    saveFormatLabel->SetFont (titleFont);

    /* save format radiobox */
    wxArrayString saveFormatChoices;
    saveFormatChoices.Add ("wave (CD quality)");
    saveFormatChoices.Add ("mp3 (192 KBits)");
    
    saveFormatRadioBox = new wxRadioBox (saveFormatPanel, wxID_ANY, "", wxPoint (10, 40), wxDefaultSize, saveFormatChoices, 2, wxRA_VERTICAL|wxNO_BORDER);
    saveFormatRadioBox->SetSelection (SAVE_TO_WAV);

    /* logarithmic panel */
    wxPanel* logarithmicPanel = new wxPanel (leftPanel, wxID_ANY);

    /* logarithmic toggle button */
    logarithmicToggleButton = new wxToggleButton (logarithmicPanel, wxID_ANY, "logarithmic VU meters", wxPoint (10, 20), wxDefaultSize, 1);
    logarithmicToggleButton->SetValue (METER_NORMAL);
    
    /* controls panel */
    wxPanel* controlsPanel = new wxPanel (leftPanel, wxID_ANY);

    /* controls buttons */
    imageRecordNoactive = new wxBitmap ("media/record_noactive.png");
    imageRecordActive = new wxBitmap ("media/record_active.png");
    imagePauseNoactive = new wxBitmap ("media/pause_noactive.png");
    imagePauseActive = new wxBitmap ("media/pause_active.png");

    recordstopButton = new wxBitmapButton (controlsPanel, wxID_ANY, *imageRecordNoactive, wxPoint (10, 0), wxSize (60,40));
    pauseButton = new wxBitmapButton (controlsPanel, wxID_ANY, *imagePauseNoactive, wxPoint (75, 0), wxSize (60,40));

    recordstopButton->Bind (wxEVT_BUTTON, &ARTopFrame::recordstop, this);
    pauseButton->Bind (wxEVT_BUTTON, &ARTopFrame::pause, this);
    
    /* controls timer label */
    timerLabel = new wxStaticText (leftPanel, wxID_ANY, "0.0", wxDefaultPosition, wxSize (120, 40), 1);
    timerLabel->SetFont (timerFont);

    /* right panel */
    wxPanel* rightPanel = new wxPanel (this, wxID_ANY);
    
    /* VU meters and clips buttons */
    colour.Set(wxT("#ececec"));

    vuContainer = new wxPanel (rightPanel);
    vuContainer->SetBackgroundColour(colour);

    colour.Set(wxT("#37DF59"));
    for (i = 0; i < aw_pcm_params.nchannels; i++)
    {
        vuMeters[i] = new wxPanel (vuContainer, wxID_ANY, wxPoint (40 * i, 0), wxSize (40, -1));
        vuMeters[i]->SetBackgroundColour(colour);
        clipsButtons[i] = new wxButton(rightPanel, wxID_HIGHEST + i, "0", wxDefaultPosition, wxSize(40, 30), 1);
        clipsButtons[i]->Bind (wxEVT_BUTTON, &ARTopFrame::resetClip, this); // TODO associare user data        
        clipsButtons[i]->SetFont (titleFont);
        snprintf(ch, sizeof ch, "ch %d", i);
        clipsLabels[i] = new wxStaticText(rightPanel, wxID_ANY, ch, wxDefaultPosition, wxSize(40, 30), 1);
    }

    updateTimer.Bind (wxEVT_TIMER, &ARTopFrame::updateStatsAndVUMeters, this);
       
     /* create layout */

    wxBoxSizer* vBox = new wxBoxSizer (wxHORIZONTAL);
    wxBoxSizer* vBoxLeft = new wxBoxSizer (wxVERTICAL);
    wxBoxSizer* hBoxControls = new wxBoxSizer (wxHORIZONTAL);
    wxBoxSizer* vBoxRight = new wxBoxSizer (wxVERTICAL);
    wxBoxSizer* hBoxClipsButtons = new wxBoxSizer (wxHORIZONTAL);
    wxBoxSizer* hBoxClipsLabels = new wxBoxSizer (wxHORIZONTAL);

    wxStaticLine* staticLine = new wxStaticLine (leftPanel, (-1,2));
    colour.Set(wxT("#edebe9"));
    staticLine->SetBackgroundColour(colour);

    vBoxLeft->Add (pcmDevicesPanel, 0, wxEXPAND|wxALL, 0);
    vBoxLeft->Add (pcmOptionsPanel, 0, wxEXPAND|wxALL, 0);
    vBoxLeft->Add (saveFormatPanel, 0, wxEXPAND|wxALL, 0);
    vBoxLeft->Add (logarithmicPanel, 0, wxEXPAND|wxALL, 0);
    vBoxLeft->Add (staticLine, 0, wxEXPAND|wxALL, 40);

    hBoxControls->Add (controlsPanel, 0, wxALIGN_LEFT, 0);
    hBoxControls->Add (timerLabel, 0, wxEXPAND|wxLEFT, 30);
    vBoxLeft->Add (hBoxControls, 1, wxEXPAND|wxALL, 10);

    for (i = 0; i < aw_pcm_params.nchannels; i++)
    {
        hBoxClipsButtons->Add (clipsButtons[i], 0, wxEXPAND|wxALL, 0);
        hBoxClipsLabels->Add (clipsLabels[i], 0, wxEXPAND|wxLEFT, 0);
    }

    vBoxRight->Add (vuContainer, 1, wxEXPAND|wxALL, 0);
    vBoxRight->Add (hBoxClipsButtons, 0, wxEXPAND|wxBOTTOM, 0);
    vBoxRight->Add (hBoxClipsLabels, 0, wxEXPAND|wxBOTTOM, 0);

    leftPanel->SetSizer (vBoxLeft);
    rightPanel->SetSizer (vBoxRight);

    vBox->Add (leftPanel, 1, wxEXPAND|wxALL, 20);
    vBox->Add (rightPanel, 0, wxEXPAND|wxALL, 20);

    SetSizer (vBox);
    vBox->Fit (this);
    Layout ();

    /* menus */

    wxMenu* menuDummy = new wxMenu; // TODO: un po più a destra
    wxMenu* menuFile = new wxMenu; // TODO: un po più a destra
    wxMenu* menuHelp = new wxMenu;

    // menuFile->Append (ID_SAVE, "&Save...\tCtrl-S", "save file on disk");
    // menuFile->Append (ID_RESET, "&Reset\tCtrl-R", "reset");
    menuFile->AppendSeparator ();
    menuFile->Append (ID_QUIT, "&Exit\tCtrl-Q", "quit");
    menuHelp->Append (ID_ABOUT, "&About", "about");
    
    menuBar = new wxMenuBar;
    menuBar->Append (menuDummy, "     ");
    menuBar->Append (menuFile, "&File");
    menuBar->Append (menuHelp, "&Help");
    SetMenuBar (menuBar);
    
    /* status bar */
    
    CreateStatusBar ();
    SetStatusText ("");
    // Bind (wxEVT_MENU, &ARTopFrame::save, this, ID_SAVE);
    // Bind (wxEVT_MENU, &ARTopFrame::reset, this, ID_RESET);
    Bind (wxEVT_MENU, &ARTopFrame::about, this, ID_ABOUT);
    Bind (wxEVT_MENU, &ARTopFrame::quit, this, ID_QUIT);

    pcmStart();
}

int ARTopFrame::updatePcmDevices ()
{
    unsigned int i;
    unsigned int j;
    AwPcm all_aw_pcms[AW_MAX_PCMS_LENGTH];
    static unsigned int all_aw_pcms_length;

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

int ARTopFrame::getPcmDevice (AwPcm** p_p_aw_pcm)
{    
    int err = -1;
    int i;

    for (i = 0; i < aw_pcms_length; i++)
        
        if (aw_pcms[i].mode == SND_PCM_STREAM_CAPTURE)
        {
            *p_p_aw_pcm = &aw_pcms[i];
            err = 0;
            break;
        }
    return err;
}

void ARTopFrame::drawPcmDevicesPanel ()
{    
    unsigned int i;
    unsigned int j;
    int selected = 0;
    wxArrayString pcmDeviceChoices;
    wxRadioBox* pcmDevicesRadioBox;

    for (i = 0; i < aw_pcms_length; i++)
    
        pcmDeviceChoices.Add (aw_pcms[i].name);
        if (aw_pcms[i].name == (*p_aw_pcm).name)
        {
            selected = i;
        }

    pcmDevicesRadioBox = new wxRadioBox (pcmDevicesPanel, wxID_ANY, "", wxPoint (10, 25), wxDefaultSize, pcmDeviceChoices, 3, wxRA_VERTICAL|wxNO_BORDER);
    pcmDevicesRadioBox->SetSelection (selected);
    pcmDevicesRadioBox->Bind (wxEVT_RADIOBOX, &ARTopFrame::changePcmDevice, this);
}

void ARTopFrame::updatePcmOptionsPanel ()
{
    unsigned int i;
    int selected;
    char val[64];
    wxArrayString choices;
    wxList childrenList;

    /* nchannels radio box */   
    selected = 0;
    aw_pcm_params.nchannels = (*p_aw_pcm).nchannels[0];
    if (pcmOptionsNChannels) pcmOptionsNChannels->Destroy ();
    
    for (i = 0; i < AW_MAX_NCHANNELS_LENGTH; i++)
    {
        if ((*p_aw_pcm).nchannels[i] == -1) break;

        snprintf (val, sizeof val, "%d", (*p_aw_pcm).nchannels[i]);
        choices.Add (val);

        if ((*p_aw_pcm).nchannels[i] == AW_DEFAULT_NCHANNELS)
        {            
            selected = i;
            aw_pcm_params.nchannels = AW_DEFAULT_NCHANNELS;
        }
    }
    pcmOptionsNChannels = new wxRadioBox (pcmOptionsPanel, wxID_ANY, "", wxPoint (80, 39), wxDefaultSize, choices, -1, wxRA_HORIZONTAL|wxNO_BORDER);
    pcmOptionsNChannels->SetSelection (selected);
    pcmOptionsNChannels->Bind (wxEVT_RADIOBOX, &ARTopFrame::changePcmOptionsNChannels, this);

    /* framerates radio box */
    
    choices.Clear();

    selected = 0;
    aw_pcm_params.framerate = (*p_aw_pcm).framerates[0];
    if (pcmOptionsFramerates) pcmOptionsFramerates->Destroy ();

    for (i = 0; i < AW_MAX_FRAMERATES_LENGTH; i++)
    {
        if ((*p_aw_pcm).framerates[i] == -1) break;

        snprintf (val, sizeof val, "%d", (*p_aw_pcm).framerates[i]);
        choices.Add (val);

        if ((*p_aw_pcm).framerates[i] == AW_DEFAULT_FRAMERATE)
        {            
            selected = i;
            aw_pcm_params.framerate = AW_DEFAULT_FRAMERATE;
        }
    }
    pcmOptionsFramerates = new wxRadioBox (pcmOptionsPanel, wxID_ANY, "", wxPoint (80, 64), wxDefaultSize, choices, -1, wxRA_HORIZONTAL|wxNO_BORDER);
    pcmOptionsFramerates->SetSelection (selected);
    pcmOptionsFramerates->Bind (wxEVT_RADIOBOX, &ARTopFrame::changePcmOptionsFramerate, this);

    /* formats radio box */

    choices.Clear();

    selected = 0;
    aw_pcm_params.format = (*p_aw_pcm).formats[0];
    if (pcmOptionsFormats) pcmOptionsFormats->Destroy ();

    for (i = 0; i < AW_MAX_FORMATS_LENGTH; i++)
    {
        if ((*p_aw_pcm).formats[i] == -1) break;

        snprintf (val, sizeof val, "%s", snd_pcm_format_name((*p_aw_pcm).formats[i]));
        choices.Add (val);

        if ((*p_aw_pcm).formats[i] == AW_DEFAULT_FORMAT)
        {            
            selected = i;
            aw_pcm_params.format = AW_DEFAULT_FORMAT;
        }
    }
    pcmOptionsFormats = new wxRadioBox (pcmOptionsPanel, wxID_ANY, "", wxPoint (80, 89), wxDefaultSize, choices, -1, wxRA_HORIZONTAL|wxNO_BORDER);
    pcmOptionsFormats->SetSelection (selected);
    pcmOptionsFormats->Bind (wxEVT_RADIOBOX, &ARTopFrame::changePcmOptionsFormat, this);
}

void ARTopFrame::changePcmDevice (wxCommandEvent& event)
{    
    unsigned int selected;

    if (state != AW_MONITORING) recordstop_ ();

    selected = event.GetSelection();
    
    if (&aw_pcms[selected] != p_aw_pcm)
    {
        // TODO: freeze gui with restart icon
        pcmStop ();
        p_aw_pcm = &aw_pcms[selected];
        updatePcmOptionsPanel ();        
        pcmStart ();
        // TODO: unfreeze gui with restart icon
    }
}

void ARTopFrame::changePcmOptionsNChannels (wxCommandEvent& event)
{    
    unsigned int selected;

    if (state != AW_MONITORING) recordstop_ ();

    selected = event.GetSelection ();
    
    if (aw_pcm_params.nchannels != (*p_aw_pcm).nchannels[selected])
    {
        // TODO: freeze gui with restart icon
        pcmStop ();
        aw_pcm_params.nchannels != (*p_aw_pcm).nchannels[selected];
        pcmStart ();
        // TODO: unfreeze gui with restart icon
    }
}

void ARTopFrame::changePcmOptionsFramerate (wxCommandEvent& event)
{    
    unsigned int selected;
 
    if (state != AW_MONITORING) recordstop_ ();

    selected = event.GetSelection ();
    
    if (aw_pcm_params.framerate != (*p_aw_pcm).framerates[selected])
    {
        // TODO: freeze gui with restart icon
        pcmStop ();
        aw_pcm_params.framerate = (*p_aw_pcm).framerates[selected];
        pcmStart ();
        // TODO: unfreeze gui with restart icon
    }
}

void ARTopFrame::changePcmOptionsFormat (wxCommandEvent& event)
{    
    unsigned int selected;

    if (state != AW_MONITORING) recordstop_ ();

    selected = event.GetSelection ();
    
    if (aw_pcm_params.format != (*p_aw_pcm).formats[selected])
    {
        // TODO: freeze gui with restart icon
        pcmStop ();
        aw_pcm_params.format != (*p_aw_pcm).formats[selected];
        pcmStart ();
        // TODO: unfreeze gui with restart icon
    }
}

void ARTopFrame::updateStatsAndVUMeters (wxTimerEvent& event)
{
    unsigned int i;
    unsigned int logarithmic;
    char value[16];
    int width;
    int height;
    float level;
    
    vuContainer->GetSize(&width, &height);
    logarithmic = logarithmicToggleButton->GetValue ();
    
    for (i = 0; i < aw_pcm_params.nchannels; i++)
    {
        if (logarithmic)
        {
            // printf("%f  -> ", ss.avg_log[i]);
            level = ss.avg_log[i] * height / 100;

        } else {
            // printf("%f  -> ", ss.avg_log[i]);
            level = ss.avg_power[i] * height / 100;
        }
        
        if (level == 0) level = 1;

        vuMeters[i]->SetSize (40 * i, height - level, 40, level);
        
        if (ss.clip[i])
        {
            clipsButtons[i]->SetForegroundColour (wxColour (* wxRED));
            clipsButtons[i]->SetLabel ("clip");

        } else {
            snprintf (value, sizeof value, "%d", (unsigned int) ss.max[i]);
            clipsButtons[i]->SetLabel (value);
        }        
    }

    if (state == AW_RECORDING)
    {
        gettimeofday (&timeRef, NULL);
        t2 = (long long) timeRef.tv_sec * 1000L + timeRef.tv_usec / 1000;
        snprintf (value, sizeof value, "%.1f", totTime + ((float) (t2 - t1)) / 1000.0);
        timerLabel->SetLabel (value);

        if (totTime > MAX_TOT_TIME) recordstop_();
    }
}

int ARTopFrame::pcmStart ()
{      
    pthread_t thread_id;

    /* open and set pcm */
    
    if ((err = snd_pcm_open (&p_pcm, (*p_aw_pcm).name, SND_PCM_STREAM_CAPTURE, SND_PCM_ASYNC)) < 0)
        return aw_handle_err (snd_strerror(err));

    if ((err = aw_set_params (p_pcm, &aw_pcm_params)) < 0)
        return aw_handle_err ("cannot set params");  

    aw_print_params (aw_pcm_params);

    if ((err = snd_pcm_prepare (p_pcm)) < 0) // serve ??? già chiamata da snd_pcm_hw_params
        return aw_handle_err(snd_strerror (err));
        
    if ((err = snd_pcm_state (p_pcm)) != SND_PCM_STATE_PREPARED)
        return aw_handle_err(snd_strerror (err));
        
    if ((err = snd_pcm_start (p_pcm)) < 0)
        return aw_handle_err (snd_strerror(err));
        
    state = AW_MONITORING;
            
    /* prepare stats struct */
    
    aw_build_compute_struct(aw_pcm_params, &ss);   
    
    /* start thread */

    thread_struct.p_pcm = p_pcm;
    thread_struct.p_hw_params = &aw_pcm_params;
    thread_struct.p_p_f = &p_f;
    thread_struct.p_ss = &ss;
    thread_struct.p_state = &state;  
    
    pthread_create (&thread_id, NULL, aw_thread_func, (void*) &thread_struct); 
    
    sleep(0.5);

    updateTimer.Start(UPDATE_TIMER_INTERVAL);  

    return 0;
}

int ARTopFrame::pcmStop ()
{      
    updateTimer.Stop();  

    state = AW_STOPPING;

    while (state != AW_STOPPED) sleep (0.1);
    
    if ((err = snd_pcm_close (p_pcm)) < 0)
        return aw_handle_err (snd_strerror(err));

    return 0;
}

int ARTopFrame::openTempFile ()
{
    static time_t t;
    struct tm* timeinfo;
    char tmppath[64];

    time (&t);
    timeinfo = localtime (&t);
    strftime (tmpname, sizeof tmpname, "rec %Y-%m-%d %H-%M-%S", timeinfo);
    snprintf (tmppath, sizeof tmppath, "/home/%s/.alsarecorder/tmp/%s", username, tmpname);

    if ((p_f = fopen (tmppath, "wb")) == NULL)
        return -1;

    return 0;
}

int ARTopFrame::closeTempFile ()
{    
    if ((fclose(p_f)) == EOF)
        return -1;    

    return 0;
}

/* controllers */

void ARTopFrame::recordstop_ ()
{
    if (state == AW_RECORDING || state == AW_PAUSED)
    {
        state = AW_MONITORING;
        recordstopButton->SetBitmap (*imageRecordNoactive);
        pauseButton->SetBitmap (*imagePauseNoactive);
        closeTempFile ();

        save_ ();

        timerLabel->SetLabel ("0.0");

    } else {
        
        recordstopButton->SetBitmap (*imageRecordActive);
        openTempFile ();
        state = AW_RECORDING;

        gettimeofday (&timeRef, NULL);
        totTime = 0;
        t1 = (long long) timeRef.tv_sec * 1000L + timeRef.tv_usec / 1000;
    }    
}

void ARTopFrame::recordstop (wxCommandEvent& event)
{
   recordstop_ (); 
}

void ARTopFrame::pause (wxCommandEvent& event)
{
    if (state == AW_PAUSED)
    {
        state = AW_RECORDING;
        pauseButton->SetBitmap (*imagePauseNoactive);

        gettimeofday (&timeRef, NULL);
        totTime = totTime + ((float) (t2 - t1)) / 1000.0;
        t1 = (long long) timeRef.tv_sec * 1000L + timeRef.tv_usec / 1000;

    } else if (state == AW_RECORDING) {

        pauseButton->SetBitmap (*imagePauseActive);
        state = AW_PAUSED;
    }   
}
    
void ARTopFrame::resetClip (wxCommandEvent& event)
{
    wxButton* eventObj = (wxButton*) event.GetEventObject ();
    unsigned int id = eventObj->GetId () - wxID_HIGHEST;

    eventObj->SetForegroundColour (wxColour(* wxBLACK));
    eventObj->SetLabel ("0");
    ss.clip[id] = false;
    ss.max[id] = 0;
}

int ARTopFrame::save_ ()
{   
    int err;
    unsigned int selected;
    char tmppath[512];
    char wavpath[512];
    char mp3path[512];
    char userpath[512];
    FILE* p_tmpf;
    FILE* p_wavf;
    long dataSize;
    long fileSize;
    u_int dummy;
    char buffer[1024];
    int bytesNum = 0;
    const unsigned int HEADERS_SIZE = 44;
    WaveHeader wh;
    wxFileDialog* saveFileDialog;

    selected = saveFormatRadioBox->GetSelection();
    
    // get paths
    snprintf (tmppath, sizeof tmppath, "/home/%s/.alsarecorder/tmp/%s", username, tmpname);

    if (selected == SAVE_TO_WAV)
    {
        snprintf (wavpath, sizeof wavpath, "/home/%s/Recordings/%s.wav", username, tmpname);
        saveFileDialog = new wxFileDialog (this, "Save Recording", "", wavpath, "wav files (*.wav)|*.wav", wxFD_SAVE|wxFD_OVERWRITE_PROMPT);

    } else {

        snprintf (wavpath, sizeof wavpath, "/home/%s/.alsarecorder/tmp/%s.wav", username, tmpname);
        snprintf (mp3path, sizeof mp3path, "/home/%s/Recordings/%s.mp3", username, tmpname);
        saveFileDialog = new wxFileDialog (this, "Save Recording", "", mp3path, "mp3 files (*.mp3)|*.mp3", wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    }

    if ((saveFileDialog->ShowModal ()) == wxID_CANCEL)
    {
        if (remove (tmppath) != 0)
            return -1;
        return -1;
    }
    snprintf (userpath, sizeof userpath, "%s", (const char*) (saveFileDialog->GetPath ()).mb_str (wxConvUTF8));
    
    // open
    if ((p_tmpf = fopen (tmppath, "rb")) == NULL)
        return -1;

    if (selected == SAVE_TO_WAV)
    {
        if ((p_wavf = fopen (userpath, "wb")) == NULL)
            return -1;

    } else {

        if ((p_wavf = fopen (wavpath, "wb")) == NULL)
            return -1;
    }

    fseek(p_tmpf, 0L, SEEK_END);
    dataSize = ftell(p_tmpf);
    fseek(p_tmpf, 0L, SEEK_SET);

    fileSize = dataSize + HEADERS_SIZE;

    wh.overall_size = fileSize - 8;
    wh.length_of_fmt = 16;
    wh.format_type = 1;
    wh.channels = aw_pcm_params.nchannels;
    wh.sample_rate = aw_pcm_params.framerate;
    wh.byterate = aw_pcm_params.byterate;
    wh.block_align = aw_pcm_params.framesize;
    wh.bits_per_sample = aw_pcm_params.real_bits;
    wh.data_size = dataSize;

    fwrite(&wh, sizeof wh, 1, p_wavf);

    // read and write rest    
    while (bytesNum >= 0 && !feof (p_tmpf))
    {
        bytesNum = fread(buffer, 1, 1024, p_tmpf);
        fwrite(buffer, 1, bytesNum, p_wavf);
    }

    // close
    if ((fclose(p_tmpf)) == EOF)
        return -1;
    if ((fclose(p_wavf)) == EOF)
        return -1;

    // remove tmp file  
    if (remove (tmppath) != 0)
        return -1;

    // transcode in mp3
    if (selected == SAVE_TO_MP3)
    {
        if (system (NULL))
        {
            snprintf(cmd, sizeof cmd, "ffmpeg -i \"%s\" -codec:a libmp3lame -qscale:a 2 \"%s\"", wavpath, userpath);
            if ((err = system(cmd)) != 0)
                printf("error in mp3 transcoding");
        }
        // remove tmp file  
        if (remove (wavpath) != 0)
            return -1;
    }
    return 0;
}

void ARTopFrame::save (wxCommandEvent& event)
{   
    save_ ();
}

void ARTopFrame::quit (wxCommandEvent& event)
{
    Close(true);
}
    
void ARTopFrame::about (wxCommandEvent& event)
{
    wxMessageBox("ALSA recorder 1.0\n\nCopyright (c) 2021 Fabio Michelini (github)\n\nPermission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.", "About", wxOK | wxICON_INFORMATION);
}


/*============================================================================
				main loop
============================================================================*/


class ARApp : public wxApp
{
public:
    bool OnInit();
};

IMPLEMENT_APP(ARApp);

bool ARApp::OnInit ()
{    
    // do some test
    if (username == NULL) return EXIT_FAILURE;

    // ensure resources    
    snprintf(cmd, sizeof cmd, "mkdir -p /home/%s/.alsarecorder/tmp", username);
    if ((err = system(cmd)) != 0)
    {
        printf("error create .alsarecorder dir\n");
        return false;
    }
    snprintf(cmd, sizeof cmd, "mkdir -p /home/%s/Recordings", username);
    if ((err = system(cmd)) != 0)
    {
        printf("error create Recordings dir\n");
        return false;
    }
    if ((err = system("fmpeg -version")) != 0)
    {
        haveMp3Transcoder = false;
        printf("no ffmpeg available: disable mp3 transcode\n");
    }

    // init and show
    ARTopFrame* topFrame = new ARTopFrame(NULL, wxID_ANY, "Alsa Recorder");
    topFrame->Show();

    return true;
}