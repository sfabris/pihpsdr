# pihpsdr
**SDR host program**,
supporting both the old (P1) and new (P2) HPSDR protocols, as well as the SoapySDR framework.
It runs on Linux (Raspberry Pi but also Desktop or Laptop computers running LINUX), macOS (Intel or AppleSilicon CPUs, using  "Homebrew" or "MacPorts"), and Windows (using MSYS2/MinGW64).

**piHPSDR Manual: (Release 2.4 Version)** https://github.com/dl1ycf/pihpsdr/releases/download/v2.4/piHPSDR-Manual.pdf

**piHPSDR Manual: (current Version)** https://github.com/dl1ycf/pihpsdr/releases/download/v2.4/piHPSDR-Manual-current.pdf

***
piHPSDR should be compiled from the sources, consult the Manual (**link given above**) on how to compile/install piHPSDR on your machine

## Windows Installation (MSYS2/MinGW64)

For Windows users, see the [Windows README](Windows/README.md) for detailed installation and build instructions.

Quick start:
1. Install MSYS2 from https://www.msys2.org/
2. Run `./Windows/install.sh` from the MinGW64 terminal
3. Copy `Windows/make.config.pihpsdr.example` to `make.config.pihpsdr`
4. Run `make clean && make`

***

Latest features:

- **client/server model for remote operation** (including transmitting in phone and CW)
- full support for Anan G2-Ultra radios, including customizable panel button/encoder functions
- added continuous frequency compressor (**CFC**) and downward expander (**DEXP**) to the TX chain
- in-depth manual (**link given above**)
- HermesLite-II I/O-board support
- audio recording (RX capture) and playback (TX)
- automatic installation procedures for compilation from the sources, for Linux (including RaspPi) and MacOS
  (Appendix J, K).
- dynamic screen resizing in the "Screen" menu, including transitions
  between full-screen and window mode



