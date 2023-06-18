#!/bin/sh
ffmpeg -i "$1" -ar 44100  -f s16le - |~/mp3encod/enco "$2" $3 $4
