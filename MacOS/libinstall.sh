#!/bin/sh

#####################################################
#
# prepeare your Macintosh for compiling piHPSDR
#
######################################################

################################################################
#
# a) MacOS does not have "realpath" so we need to fiddle around
#
################################################################

THISDIR="$(cd "$(dirname "$0")" && pwd -P)"

################################################################
#
# b) Initialize HomeBrew and required packages
#    (this does no harm if HomeBrew is already installed)
#
################################################################
  
#
# This installes the core of the homebrew universe
#
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"

#
# At this point, there is a "brew" command either in /usr/local/bin (Intel Mac) or in
# /opt/homebrew/bin (Silicon Mac). Look what applies, and set the variable OPTHOMEBREW to 1
# if homebrew is installed in /opt/homebrew rather than in /usr/local.
# Prepeare the environment variables CPATH and LIBRARY_PATH. These are usually not needed
# for Intel Macs, but if "homebrew" is installed in /opt/homebrew, these guarantee
# that include files are found by the preprocessor, and libraries are found by the linker.
#
BREW=junk
OPTHOMEBREW=0

if [ -x /usr/local/bin/brew ]; then
  BREW=/usr/local/bin/brew
  if [ z$CPATH == z ]; then
    export CPATH=/usr/local/include
  else
    export CPATH=$CPATH:/usr/local/include
  fi
  if [ z$LIBRARY_PATH == z ]; then
    export LIBRARY_PATH=/usr/local/lib
  else
    export LIBRARY_PATH=$LIBRARY_PATH:/usr/local/lib
  fi
fi

if [ -x /opt/homebrew/bin/brew ]; then
  BREW=/opt/homebrew/bin/brew
  OPTHOMEBREW=1
  if [ z$CPATH == z ]; then
    export CPATH=/opt/homebrew/include
  else
    export CPATH=$CPATH:/opt/homebrew/include
  fi
  if [ z$LIBRARY_PATH == z ]; then
    export LIBRARY_PATH=/opt/homebrew/lib
  else
    export LIBRARY_PATH=$LIBRARY_PATH:/opt/homebrew/lib
  fi
fi

if [ $BREW == "junk" ]; then
  echo HomeBrew installation obviously failed, exiting
  exit
fi

################################################################
#
# This adjusts the PATH in the shell startup file, together with
# CPATH and LIBRARY_PATH. This is not bullet-proof, so if some-
# thing goes wrong here, the user will later not find the
# 'brew' command. 
#
################################################################

if [ $SHELL == "/bin/sh" ]; then
$BREW shellenv sh >> $HOME/.profile
echo "export CPATH=$CPATH" >> $HOME/.profile
echo "export LIBRARY_PATH=$LIBRARY_PATH" >> $HOME/.profile
fi
if [ $SHELL == "/bin/csh" ]; then
$BREW shellenv csh >> $HOME/.cshrc
echo "setenv CPATH $CPATH" >> $HOME/.cshrc
echo "setenv LIBRARY_PATH $LIBRARY_PATH" >> $HOME/.cshrc
fi
if [ $SHELL == "/bin/zsh" ]; then
$BREW shellenv zsh >> $HOME/.profile
echo "export CPATH=$CPATH" >> $HOME/.zprofile
echo "export LIBRARY_PATH=$LIBRARY_PATH" >> $HOME/.profile
fi

################################################################
#
# All homebrew packages needed for pihpsdr (makedepend and
# cppcheck are useful for maintainers)
#
################################################################
$BREW install gtk+3
$BREW install librsvg
$BREW install pkg-config
$BREW install portaudio
$BREW install fftw
$BREW install libusb
$BREW install makedepend
$BREW install cppcheck
$BREW install openssl@3

################################################################
#
# This is for the SoapySDR universe
# There are even more radios supported for which you need
# additional modules, for a list, goto the web page
# https://formulae.brew.sh
# and insert the search string "pothosware". In the long
# list produced, search for the same string using the
# "search" facility of your internet browser
#
$BREW install cmake
$BREW install python-setuptools
#
# If an older version of SoapySDR exist, a forced
# re-install may be necessary (note parts the Soapy stuff
# is always compiled from the sources).
#
$BREW tap pothosware/pothos
$BREW reinstall soapysdr
$BREW reinstall pothosware/pothos/soapyplutosdr
$BREW reinstall pothosware/pothos/limesuite
$BREW reinstall pothosware/pothos/soapyrtlsdr
$BREW reinstall pothosware/pothos/soapyairspy
#
# NOTE: due to an error in the homebrew formula, airspayhf will not build
#       on M2 macs since its homebrew formula contains an explicit reference
#       to /usr/local. If /usr/local is sym-linked to /opt/homebrew/local,
#       it works fine but this cannot be enforced.
#
$BREW reinstall pothosware/pothos/soapyairspyhf
$BREW reinstall pothosware/pothos/soapyhackrf
$BREW reinstall pothosware/pothos/soapyredpitaya
$BREW reinstall pothosware/pothos/soapyrtlsdr

################################################################
#
# This is for PrivacyProtection
#
################################################################
$BREW analytics off

