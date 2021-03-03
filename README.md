## Alsa Recorder
Simple recorder application based on alsa.  
Expose all audio cards with a selection of "popular" options (channels, framerate, format).  
Logarithmic VU meters, clipping and peak facilities, wav and mp3 save formats.  
Micro library alsawrapper provide some alsa facilities.
  
![Alsa Recorder Logo](/media/alsarecorder-icon.png)

### dependencies
* ```libgtk-3-0``` and ```libgtk-3-dev``` required for compile
* ```libasound2-dev``` required for compile
* ```ffmpeg``` optional for mp3 save format

### compilation
Download zip, unpack and compile with:  
  
```gcc -rdynamic -no-pie `pkg-config --cflags gtk+-3.0` -o alsarecorder alsarecorder.c alsawrapper.c `pkg-config --libs gtk+-3.0` -lasound -lpthread -lm```  
  
Than make it executable with ```chmod +x alsarecorder``` and double click on it.

### pre-build
Download zip, unpack and double click on pre-builded executable ```alsarecorder```.

### launcher
Customize and put ```alsarecorder.desktop```launcher in ```/.local/share/applications/``` under your home or in ```/usr/share/applications/``` for all users.

### screenshot
<img src="/media/screenshot.png" width="300" />

### to do
* save in memory with iostream  