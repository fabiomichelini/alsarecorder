# alsarecorder
Simple recorder application based on alsa

# compilation
g++ `wx-config --cxxflags` alsarecorder.cpp alsawrapper.c -lasound -lpthread -lm `wx-config --libs` -o alsarecorder

# pre-build
pre-builded alsarecorder executable is for Debian 10
