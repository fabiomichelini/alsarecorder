### alsarecorder
Simple recorder application based on alsa.

![Alsa Recorder Logo](/media/alsarecorder-icon.png)

### dependencies
* libgtk-3-0 libgtk-3-dev
* libasound2-dev
* ffmpeg

### compilation
```gcc -rdynamic -no-pie `pkg-config --cflags gtk+-3.0` -o alsarecorder alsarecorder.c alsawrapper.c `pkg-config --libs gtk+-3.0` -lasound -lpthread -lm```

### pre-build
Just copy:    
* alsarecorder  
* alsarecorder.glade  
* alsarecorder.css  
* media

in your application folder and move  
  
* alsarecorder.desktop
  
in /usr/share/applications/ or in /.local/share/applications under your home dir. Pre-builded alsarecorder executable is tested on Debian 10.

### to do
* replace arrays with malloc and free  
* save in memory with iostream  
