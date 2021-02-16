# alsarecorder
Simple recorder application based on alsa

# complation
g++ `wx-config --cxxflags` alsarecorder.cpp alsawrapper.c -lasound -lpthread -lm `wx-config --libs` -o alsarecorder

# pre build
alsarecorder is for Debian 10
