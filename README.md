# pihpsdr
Standalone code for HPSDR,
supporting both the old (P1) and new (P2) HPSDR protocols, as well as the SoapySDR framework.

It runs on Linux (including RaspPi 3/4/5) and MacOS (using the "Homebrew" working environment).

**Consult the Manual (Appendix J,  K, L) on how-to install and or compile piHPSDR
on your machine (Appendix J: Raspberry PI binary installation, Appendix K: LINUX compile from
sources, Appendix L: MacOS compile from sources).**

Latest features:

- in-depth (about pages) manual (file release/piHPSDR-Manual.pdf)
- automatic installation procedures, a binary-only installation for RaspPi only,
  and a "compile from sources" procedure for Linux (including RaspPi) and MacOS
  (Appendix J, K, L).
- dynamic screen resizing in the "Screen" menu, including transitions
  between full-screen and window mode
- PureSignal now works with Anan-10E/100B in P1
- HermesLite-II drive slider now working smoothly
- CW audio peak filter (in the Filter menu)
- Improved layout for nearly all menus
- Option to make pop-down menus (combo boxes) "TouchScreen-Friendly" (Radio Menu)

Full source code download using git:
git clone https://github.com/dl1ycf/pihpsdr.git

**Installation by compiling from the sources (see the Manual,
 Appendix K for Linux and Appendix L for MacOS) is highly recommended,
since the binary-only installation (see the Manual, Appendix J)
is not much easier, and binaries may cease to work across OS upgrades.**

