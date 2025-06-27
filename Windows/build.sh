#!/bin/bash

#####################################################
#
# Complete build script for piHPSDR on Windows
# using MSYS2/MinGW64
#
######################################################

echo "piHPSDR Windows Build Script"
echo "============================"

# Check if we're running in MSYS2
if [ -z "$MSYSTEM" ]; then
    echo "Error: This script must be run from MSYS2/MinGW64 environment"
    echo "Please install MSYS2 from https://www.msys2.org/ and run from MinGW64 terminal"
    exit 1
fi

echo "Detected MSYS2 environment: $MSYSTEM"

# Check if we're in the right directory
if [ ! -f "Makefile" ] || [ ! -d "src" ]; then
    echo "Error: This script must be run from the piHPSDR root directory"
    exit 1
fi

# Install dependencies
echo "Installing dependencies..."
if [ ! -f "Windows/install.sh" ]; then
    echo "Error: Windows/install.sh not found"
    exit 1
fi

chmod +x Windows/install.sh
./Windows/install.sh

if [ $? -ne 0 ]; then
    echo "Error: Failed to install dependencies"
    exit 1
fi

# Create configuration file if it doesn't exist
if [ ! -f "make.config.pihpsdr" ]; then
    echo "Creating make.config.pihpsdr from example..."
    cp Windows/make.config.pihpsdr.example make.config.pihpsdr
    echo "Configuration file created. You can edit make.config.pihpsdr to customize build options."
else
    echo "Found existing make.config.pihpsdr file"
    echo "Current configuration:"
    cat make.config.pihpsdr
fi

# Build WDSP library first
echo "Building WDSP library..."
cd wdsp
make clean
make

if [ $? -ne 0 ]; then
    echo "Error: Failed to build WDSP library"
    exit 1
fi

cd ..

# Build piHPSDR
echo "Building piHPSDR..."
make clean
make

if [ $? -ne 0 ]; then
    echo "Error: Failed to build piHPSDR"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo ""
echo "To run piHPSDR:"
echo "1. Make sure all MinGW64 DLLs are in your PATH"
echo "2. Run: ./pihpsdr.exe"
echo ""
echo "For distribution, you'll need to copy the required DLLs:"
echo "- All MinGW64 runtime DLLs"
echo "- GTK3 runtime files"
echo "- Audio library DLLs"
echo ""
echo "Check Windows/README.md for more information."
