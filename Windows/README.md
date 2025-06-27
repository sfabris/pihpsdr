# piHPSDR Windows Port

This directory contains files for building piHPSDR on Windows using MSYS2/MinGW64.

## Prerequisites

1. **Install MSYS2**: Download and install from https://www.msys2.org/
2. **Open MinGW64 terminal**: Use the "MSYS2 MinGW 64-bit" terminal (not the regular MSYS2 terminal)

## Installation

1. Run the installation script to install all required dependencies:
   ```bash
   ./Windows/install.sh
   ```

2. Copy the example configuration file:
   ```bash
   cp Windows/make.config.pihpsdr.example make.config.pihpsdr
   ```

3. Edit `make.config.pihpsdr` if you want to customize build options.

## Building

From the piHPSDR root directory in the MinGW64 terminal:

```bash
make clean
make
```

## Windows-Specific Features

### Thread and Semaphore Compatibility
- Full POSIX thread (pthread) emulation using Windows threads
- POSIX semaphore emulation using Windows semaphores
- Automatic compatibility layer included during compilation

### Network Compatibility  
- Winsock2 wrapper for POSIX socket functions
- Automatic socket initialization and cleanup
- Full network functionality maintained

### Audio
- Uses PortAudio for cross-platform audio support
- Supports all Windows audio devices (DirectSound, WASAPI, etc.)

### MIDI
- Uses Windows Multimedia API (winmm) for MIDI support
- Supports all Windows MIDI devices

### Networking
- Full network support for HPSDR protocols
- Client/server functionality
- Web interface for RedPitaya devices

### USB Support
- USB OZY support via libusb
- SoapySDR support (optional)

## Limitations

The following features are not available on Windows:

- **GPIO**: Raspberry Pi GPIO support (Linux-only)
- **SATURN**: Native SATURN/G2 XDMA support (Linux-only)

## Dependencies

The following libraries are automatically installed by the install script:

- GTK3+ (GUI framework)
- PortAudio (audio)
- FFTW (DSP)
- OpenSSL (encryption)
- libusb (USB devices)
- cURL (web functionality)
- WinMM (Windows MIDI)

## Troubleshooting

### Build Issues

1. **Missing pkg-config**: Make sure you're using the MinGW64 terminal
2. **Library not found**: Re-run the install script
3. **Permission errors**: Run MinGW64 as administrator

### Runtime Issues

1. **DLL not found**: Make sure MinGW64/bin is in your PATH
2. **Audio issues**: Check Windows audio device settings
3. **Network issues**: Check Windows Firewall settings

### Getting Help

- Check the main piHPSDR manual for general usage
- Report Windows-specific issues on the GitHub repository
- Include your Windows version and MSYS2 version in bug reports

## Building SoapySDR Modules (Optional)

If you want SoapySDR support, you may need to build additional modules:

```bash
# Install additional SoapySDR modules
pacman -S mingw-w64-x86_64-soapysdr-module-rtlsdr
pacman -S mingw-w64-x86_64-soapysdr-module-airspy
# Add other modules as needed
```

Then rebuild with:
```bash
echo "SOAPYSDR=ON" >> make.config.pihpsdr
make clean
make
```

## Creating a Distribution

To create a redistributable package:

1. Copy the built executable and all required DLLs
2. Include GTK3 runtime files
3. Package with an installer like NSIS

A future update may include automated packaging scripts.
