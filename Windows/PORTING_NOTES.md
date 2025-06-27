# piHPSDR Windows Port Summary

This document summarizes the Windows port implementation for piHPSDR using MSYS2/MinGW64.

## Files Added/Modified

### New Files Created

1. **Windows/install.sh** - Automated installation script for MSYS2 dependencies
2. **Windows/build.sh** - Complete build script for Windows
3. **Windows/README.md** - Detailed Windows-specific documentation
4. **Windows/make.config.pihpsdr.example** - Example configuration file for Windows
5. **src/windows_midi.c** - Windows MIDI support using WinMM API
6. **src/windows_compat.h** - Windows compatibility layer header
7. **src/windows_compat.c** - Windows compatibility implementations

### Modified Files

1. **Makefile** - Added Windows platform detection and build support
2. **README.md** - Added Windows installation information
3. **src/main.c** - Added Windows socket initialization and compatibility

## Platform-Specific Features

### Supported on Windows
- ✅ GTK3 GUI (full functionality)
- ✅ Audio via PortAudio (all Windows audio systems)
- ✅ MIDI via Windows Multimedia API
- ✅ Network protocols (P1/P2 HPSDR)
- ✅ Client/server functionality  
- ✅ USB OZY support via libusb
- ✅ SoapySDR support (optional)
- ✅ RedPitaya/STEMlab web interface
- ✅ Text-to-speech (basic support)
- ✅ WDSP DSP library

### Not Supported on Windows
- ❌ GPIO (Raspberry Pi specific)
- ❌ SATURN/G2 XDMA (Linux kernel driver specific)

## Architecture

### Windows Compatibility Layer
- **Socket API**: Winsock2 wrapper for POSIX socket functions
- **Clock Functions**: Windows implementation of clock_gettime()
- **Process Functions**: Windows equivalents for POSIX process functions
- **Path Handling**: Windows path separator handling

### MIDI Implementation
- Uses Windows Multimedia API (winmm.dll)
- Supports all Windows MIDI devices
- Asynchronous callback-based processing
- Full compatibility with existing MIDI menu system

### Audio Implementation
- Uses PortAudio library
- Supports DirectSound, WASAPI, MME drivers
- Cross-platform audio device enumeration
- Low-latency audio processing

## Build System

### Dependencies
- MSYS2/MinGW64 toolchain
- GTK3+ development libraries
- PortAudio audio library
- FFTW3 DSP library
- OpenSSL cryptography library
- libusb USB device library
- WinMM MIDI library (built-in Windows)

### Build Process
1. Install MSYS2
2. Run dependency installation script
3. Configure build options
4. Build WDSP library
5. Build main application

### Configuration Options
- Audio: PortAudio (only supported option)
- MIDI: Windows WinMM API
- GPIO: Disabled
- SATURN: Disabled
- SoapySDR: Optional
- USB OZY: Supported
- STEMlab: Supported

## Testing Recommendations

### Basic Functionality
1. GUI startup and operation
2. Audio input/output
3. MIDI device detection and operation
4. Network discovery and connection
5. USB device detection
6. Configuration save/restore

### Network Testing
1. HPSDR protocol compatibility
2. Client/server mode
3. Multiple radio support
4. RedPitaya web interface

### Audio Testing
1. Multiple audio devices
2. Sample rate changes
3. Latency optimization
4. Driver compatibility (DirectSound, WASAPI)

## Known Limitations

1. **Path Separators**: Some file operations may need additional Windows path handling
2. **Real-time Priority**: Windows scheduling differs from Linux SCHED_FIFO
3. **Firewall**: Windows Firewall may block network operations
4. **DLL Dependencies**: Runtime requires proper DLL distribution

## Future Enhancements

1. **Installer**: Create NSIS or WiX installer package
2. **Auto-DLL**: Automatic DLL bundling for distribution
3. **Windows Services**: Optional Windows service mode
4. **Registry Integration**: Windows registry settings support
5. **COM Integration**: Windows COM object support for automation

## Compatibility Matrix

| Feature | Linux | macOS | Windows |
|---------|-------|-------|---------|
| GUI | GTK3 | GTK3 | GTK3 |
| Audio | ALSA/Pulse | PortAudio | PortAudio |
| MIDI | ALSA | CoreMIDI | WinMM |
| Network | Full | Full | Full |
| GPIO | Yes | No | No |
| SATURN | Yes | No | No |
| USB | libusb | libusb | libusb |
| SoapySDR | Yes | Yes | Optional |

## Integration Notes

This Windows port maintains full API compatibility with the existing Linux and macOS versions. The abstraction layer ensures that platform-specific code is isolated and the core application logic remains unchanged.

The port follows the existing architectural patterns and coding standards used in the macOS port, making it a natural extension of the existing cross-platform design.
