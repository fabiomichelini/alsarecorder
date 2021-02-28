### alsarecorder
Simple recorder application based on alsa.  
  
![Alsa Recorder Logo](/media/alsarecorder-icon.png)

### dependencies
* libgtk-3-0 libgtk-3-dev
* libasound2-dev
* ffmpeg

### compilation
Just download zip, unpack and run:  
```gcc -rdynamic -no-pie `pkg-config --cflags gtk+-3.0` -o alsarecorder alsarecorder.c alsawrapper.c `pkg-config --libs gtk+-3.0` -lasound -lpthread -lm```

### pre-build
Just download zip, unpack and run pre-builded executable ```alsarecorder```. Is tested on Debian 10.

### screenshot
<img src="/media/screenshot.png" width="300" />

### to do
* replace arrays with malloc and free  
* save in memory with iostream  
