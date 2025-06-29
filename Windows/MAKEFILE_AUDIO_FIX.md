# Makefile Audio Backend Fix Summary

## Issues Fixed

### 1. **Incorrect Hardcoded Audio Backend**
- **Problem**: Line 21 had `AUDIO=PORTAUDIO` hardcoded, which overrode the OS-specific logic
- **Solution**: Removed the hardcoded assignment to allow OS-specific defaults to work

### 2. **Always Compiling All Audio Backends**
- **Problem**: Audio backend defines (`-DPULSEAUDIO`, `-DALSA`, `-DPORTAUDIO`) were always added regardless of the `$(AUDIO)` setting
- **Solution**: Moved the defines inside their respective conditional blocks

## Current Behavior (Fixed)

### Default Audio Backends by OS:
- **Linux**: `PULSE` (PulseAudio) - unless overridden with `AUDIO=ALSA`
- **macOS**: `PORTAUDIO` (only option)
- **Windows**: `PORTAUDIO` (only option)

### To Override Defaults:
Create a `make.config.pihpsdr` file with:
```makefile
AUDIO=ALSA      # For Linux ALSA instead of PulseAudio
AUDIO=PULSE     # For Linux PulseAudio (default anyway)
AUDIO=PORTAUDIO # For any OS to use PortAudio
```

## Changes Made

### 1. Removed Hardcoded Default
```diff
- AUDIO=PORTAUDIO
```

### 2. Fixed Conditional Compilation
```diff
ifeq ($(AUDIO), PULSE)
AUDIO_OPTIONS=-DPULSEAUDIO
# ... other PULSE settings ...
+ CPP_DEFINES += -DPULSEAUDIO
+ CPP_SOURCES += src/pulseaudio.c
endif
- CPP_DEFINES += -DPULSEAUDIO
- CPP_SOURCES += src/pulseaudio.c
```

Same pattern applied to ALSA and PORTAUDIO blocks.

### 3. Updated Documentation
- Clarified audio backend options in comments
- Added explanation of OS-specific defaults

## Verification

Now when you build:
- On Linux: Will use PulseAudio by default (compiles `src/pulseaudio.c` with `-DPULSEAUDIO`)
- With `AUDIO=ALSA`: Will use ALSA (compiles `src/audio.c` with `-DALSA`)
- With `AUDIO=PORTAUDIO`: Will use PortAudio (compiles `src/portaudio.c` with `-DPORTAUDIO`)
- On Windows/macOS: Will always use PortAudio

Only the selected audio backend will be compiled and linked, not all of them.
