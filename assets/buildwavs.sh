#!/bin/bash

export HERE=${PWD} &&
export TARGET=../src/res &&

ffmpeg -y -i ${HERE}/tada.wav -filter:a "volume=0.60" -c:a adpcm_ms -ar 44100 -ac 2 ${TARGET}/tada.wav &&

ffmpeg -y -i ${HERE}/imgburn_ohno.wav -filter:a "volume=0.60" -c:a adpcm_ms -ar 44100 -ac 2 ${TARGET}/ohno.wav &&

ffmpeg -y -i ${HERE}/ding.wav -filter:a "volume=1.00" -c:a adpcm_ms -ar 44100 -ac 2 ${TARGET}/notify.wav &&

exit 0
