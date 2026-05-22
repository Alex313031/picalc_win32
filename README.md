# PiCalc Win32 &nbsp;<img src="./assets/48.png" height="38">

 - A Win32 Pi Calculator, for stress testing or fun, reminiscent of [SuperPi](https://en.wikipedia.org/wiki/Super_PI);

<p align="center">
<img src="https://raw.githubusercontent.com/Alex313031/picalc_win32/refs/heads/main/assets/screenshot.png" width="50%">
</p>

## Motivation

Because I wanted an open source version of SuperPi.

It is multi-threaded, and designed to be hyper-compatible - it supports Windows 2000 - 11.  
It uses the [GMP](https://gmplib.org/) library for large floating point math.

<img src="./assets/Windows_2000_logo.svg" height="64"> :smile_cat:

## Building

### With GN/Ninja
[Chromium](https://www.chromium.org) uses a build system with [GN](https://gn.googlesource.com/gn/+/refs/heads/main/README.md) and [Ninja](https://ninja-build.org/).

You will have to have my [fork of GMP for Ninja](https://github.com/Alex313031/gmp-ninja) in ./src.

I have made a minimal, modified version configured specifically for compiling Win32 programs
for legacy Windows called [gn-legacy](https://github.com/Alex313031/gn-legacy).  
It can be used on Windows 7+ or Linux. (Unlike the regular MinGW method above, gn.exe does not work on Windows XP/Vista.)

Really, it is a meta-build system. GN stands for "Generate Ninja" and can use __BUILD.gn__ files to
generate `.ninja` files. These are used by Ninja (the actual build system), to run the commands to compile it.  
The compiler itself is dependant on the host platform:  
On Linux, a special MinGW build I compiled on Ubuntu 24.04 to support legacy Windows and use static linkage is used.
On Windows, it simply uses an extracted toolchain from win32-devkit mentioned above.

## Resources

[Download SuperPi](https://superpi.ilbello.com/)

Charles Petzold - [Programming Windows 5th Ed.](https://www.charlespetzold.com/books/)
