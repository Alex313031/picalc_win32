#!/bin/bash

# Current dir (where this script lives, not where it was invoked)
export HERE=$(cd "$(dirname "$0")" && pwd) &&

# Go up one dir to root of repo
export ROOT="$(dirname -- "$HERE")"

# src resources directory
export TARGET=${ROOT}/src/res &&

# Convert files into Windows 2000+ compatible .wavs, with custom volume
ffmpeg -y -i ${HERE}/tada.wav -filter:a "volume=0.60" -c:a adpcm_ms -ar 44100 -ac 2 ${TARGET}/tada.wav &&

ffmpeg -y -i ${HERE}/imgburn_ohno.wav -filter:a "volume=0.60" -c:a adpcm_ms -ar 44100 -ac 2 ${TARGET}/ohno.wav &&

ffmpeg -y -i ${HERE}/ding.wav -filter:a "volume=1.00" -c:a adpcm_ms -ar 44100 -ac 2 ${TARGET}/notify.wav &&

exit 0
