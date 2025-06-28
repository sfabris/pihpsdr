# Windows Port Modifications Restored

## Summary of Changes Restored

### 1. startup.c Modifications
- **Windows compatibility includes**: Added conditional includes for Windows compatibility layer
- **Windows working directory**: Uses `%USERPROFILE%/Documents/piHPSDR` on Windows instead of `~/.config/pihpsdr`
- **Enhanced debug output**: Added Windows-specific debug messages to trace startup process
- **File redirection notice**: Added debug output explaining that stdout/stderr are redirected to files after startup

### 2. portaudio.c Debug Enhancements
- **File-based debug output**: All PortAudio debug output is now written to `audio_debug.txt` file
- **Enhanced host API detection**: Detailed debug output for all PortAudio host APIs (ASIO, WASAPI, DirectSound, etc.)
- **Device enumeration debug**: Comprehensive debug output for audio input/output device detection
- **Cross-platform debug**: Debug output works both in console and file on all platforms

## Key Insight: Output Redirection
After the `startup()` function completes, piHPSDR redirects stdout and stderr to files:
- `pihpsdr.stdout` - Contains all standard output including PortAudio debug info
- `pihpsdr.stderr` - Contains all error output

On Windows, these files will be located in `%USERPROFILE%/Documents/piHPSDR/`

## How to Verify Audio Drivers on Windows
1. Run piHPSDR
2. After startup, check the following files in the working directory:
   - `audio_debug.txt` - Immediate PortAudio debug output
   - `pihpsdr.stdout` - All program output after startup
   - `pihpsdr.stderr` - All error output after startup

## Expected Output
Look for entries like:
```
Host API 0: Windows DirectSound (DirectSound) - 4 devices
Host API 1: Windows WASAPI (WASAPI) - 8 devices  
Host API 2: ASIO (ASIO) - 2 devices
INPUT DEVICE, No=5, Name=ASIO Driver [ASIO], MaxInputChannels=2
OUTPUT DEVICE, No=6, Name=ASIO Driver [ASIO], MaxOutputChannels=2
```

This confirms that ASIO and other Windows audio drivers are properly detected.

## Files Modified
- `/home/sf/pihpsdr/src/startup.c` - Windows compatibility and debug output
- `/home/sf/pihpsdr/src/portaudio.c` - Enhanced debug output and file logging
- Windows compatibility layer already exists in `/home/sf/pihpsdr/Windows/`

## Build Instructions
The existing Makefile should build properly with these modifications on Windows using MinGW/MSYS2.
