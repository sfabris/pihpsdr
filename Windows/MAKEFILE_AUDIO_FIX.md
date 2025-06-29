# Makefile Audio Backend Fix Summary

## Issues Fixed

### 1. **Incorrect Hardcoded Audio Backend**
- **Problem**: Line 21 had `AUDIO=PORTAUDIO` hardcoded, which overrode the OS-specific logic
- **Solution**: Changed to `AUDIO=` (empty) to allow OS-specific defaults to work

### 2. **Always Compiling All Audio Backends**
- **Problem**: Audio backend defines (`-DPULSEAUDIO`, `-DALSA`, `-DPORTAUDIO`) were always added regardless of the `$(AUDIO)` setting
- **Solution**: Moved the defines inside their respective conditional blocks

### 3. **Improved Logic for Empty AUDIO Setting**
- **Problem**: Logic didn't handle empty AUDIO setting properly
- **Solution**: Added explicit check for empty AUDIO on Linux

## Current Behavior (Fixed)

### How to Set Audio Backend:

#### Method 1: Edit Makefile directly
```makefile
AUDIO=PULSE     # Use PulseAudio
AUDIO=ALSA      # Use ALSA
AUDIO=PORTAUDIO # Use PortAudio
AUDIO=          # Use OS default (Linux=PULSE, macOS/Windows=PORTAUDIO)
```

#### Method 2: Create make.config.pihpsdr file
```makefile
AUDIO=PULSE     # For Linux PulseAudio
AUDIO=ALSA      # For Linux ALSA instead of PulseAudio
AUDIO=PORTAUDIO # For any OS to use PortAudio
```

#### Method 3: Command line override
```bash
make AUDIO=PULSE
make AUDIO=ALSA
make AUDIO=PORTAUDIO
```

## Troubleshooting

### If you see `-DPORTAUDIO` when you set `AUDIO=PULSE`:

1. **Check your setting**: Run `make -n | grep AUDIO` to see what audio backend is detected
2. **Clean build**: Run `make clean` then `make` to ensure old objects are rebuilt
3. **Verify OS detection**: Check if `UNAME_S` is detected correctly as `Linux`

### Debug Commands:
```bash
# See what AUDIO backend is selected
make -n | head -20 | grep AUDIO

# See OS detection
make -n | head -10 | grep UNAME

# Clean build
make clean && make AUDIO=PULSE
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
