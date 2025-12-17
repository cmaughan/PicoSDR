[BlackBox SDR Software](https://github.com/cmaughan/PicoSDR) - BlackBox SDR Software 

Current Video Demo:
https://youtu.be/GtFvweOVEOg?si=ePjZIFveJN3LmeJ_

This is a work in progress software suite for controlling my BlackBox SDR project.  At time of writing this consists of a Pi Pico driver which makes the Pi into a USB Microphone/Midi/Bulk Transfer device, and a TestBed application for exercising the features from the PC end.  Eventually there will be the radio application itself.

I tend to rough out solutions and then aggressively refactor stuff over time.  That means this code changes a lot.

Design
======
The radio is designed as a block box with an RF connector, a power light, and a USB connection (first prototype: transmitter comes later).  The radio controls, display, and decoding will be done on your phone or PC.  The hardware does:

1. Band Pass Filter
2. RF Amplification
2. Mixing (to audio frequency; initially 12Khz bandwidth - later higher)
3. IF Amplification
4. Low Pass Filter
5. ADC

There is no Tayloe detector.  All the work of decoding (initially CW only) is on the PC side.
Why?  Because the CPU, the high res display and the amazing hardware you hold in your pocket daily (your phone!) is way better than anything you can build onto a portable SDR device.

TestBed
=======
The test bed app currently receives the Pico audio (if you set the audio input correctly), can ask for a new 'frequency' over MIDI (currently for testing an AF signal).  There is a menu option to grab a profile from the Pico (initially the profile is the testbed app).  See the video link above for a demo of how things work.

Profiler
========
For those interested, there is a simple Pico profiler which enables collection of a threaded tree of work on the Pi in graphical form.  You can find the Pico end of this in:

\target\external\pico_zest\include\pico_zest\time\pico_profiler.h
\target\external\pico_zest\src\time\pico_profiler.cpp

.. the PC side code is in

\target\external\zest\src\time\profiler.cpp

Here's how it works 
1. Using the bulk transfer interface, the PC requests a profile is kicked off (via the menu)
2. The pico starts collecting callstacks, and fills up fixed memory buffers until they are full.
3. The pico uses the bulk transfer to send a serialized form of the profiler data to the PC
4. The PC overwrites its collected profiler data with the Pico data and shows it.
(look in musb_vendor.cpp for the target handling)


