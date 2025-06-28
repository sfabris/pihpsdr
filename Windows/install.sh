#!/bin/bash

#####################################################
#
# Prepare your Windows system for compiling piHPSDR
# using MSYS2/MinGW64
#
######################################################

echo "piHPSDR Windows/MinGW64 Installation Script"
echo "============================================"

# Check if we're running in MSYS2
if [ -z "$MSYSTEM" ]; then
    echo "Error: This script must be run from MSYS2/MinGW64 environment"
    echo "Please install MSYS2 from https://www.msys2.org/ and run from MinGW64 terminal"
    exit 1
fi

echo "Detected MSYS2 environment: $MSYSTEM"

# Update package database
echo "Updating package database..."
pacman -Syu --noconfirm

# Install build tools
echo "Installing build tools..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-make \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-pkg-config \
    mingw-w64-x86_64-toolchain \
    make \
    git

# Install GTK3 and dependencies
echo "Installing GTK3 and GUI dependencies..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-gtk3 \
    mingw-w64-x86_64-glib2 \
    mingw-w64-x86_64-cairo \
    mingw-w64-x86_64-pango \
    mingw-w64-x86_64-gdk-pixbuf2 \
    mingw-w64-x86_64-librsvg

# Install audio libraries
echo "Installing audio libraries..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-portaudio \
    mingw-w64-x86_64-libsndfile

# Install FFTW for DSP
echo "Installing FFTW..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-fftw

# Install OpenSSL
echo "Installing OpenSSL..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-openssl

# Install USB libraries
echo "Installing USB libraries..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-libusb

# Install CURL for web functionality
echo "Installing CURL..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-curl

# Install optional: MIDI support libraries
echo "Installing MIDI support..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-rtmidi

# Install SoapySDR if available
echo "Installing SoapySDR (if available)..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-soapysdr || echo "SoapySDR not available in standard repos"

echo ""
echo "Installation completed!"
echo ""
echo "To compile piHPSDR:"
echo "1. Open MinGW64 terminal"
echo "2. Navigate to the piHPSDR directory"
echo "3. Create a make.config.pihpsdr file with Windows-specific options"
echo "4. Run: make clean && make"
echo ""
echo "Example make.config.pihpsdr for Windows:"
echo "AUDIO=PORTAUDIO"
echo "GPIO=OFF"
echo "SATURN=OFF"
echo "MIDI=ON"
echo "SOAPYSDR=OFF"
echo "USBOZY=ON"
echo "STEMLAB=ON"
