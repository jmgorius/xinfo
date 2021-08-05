# xinfo

A command line tool with no external dependencies to print information about an
X server instance.

## Building and running

To build the code in this repository, run
```console
$ make
```
The latter command produces the `xinfo` executable, which can be run to get
information about the X server running for the current session.
```console
$ ./xinfo
```
A different X display can be selected by setting the `DISPLAY` environment
variable such that
```console
$ DISPLAY=hostname:D.S ./xinfo
```
returns information about display `D` and screen `S` on host `hostname`.

## Sample output

Here is an example of the output produced by `xinfo`.
```
xinfo - X server information printer

Vendor....................................... The X.Org Foundation
Version...................................... 11.0
Release number............................... 1.20.13

Resource ID base............................. 0x04000000
Resource ID mask............................. 0x001fffff
Motion buffer size........................... 256
Maximum request length....................... 16777212 bytes
Image byte order............................. little endian
Bitmap format bit order...................... least significant first
Bitmap format scanline unit.................. 32
Bitmap format scanline pad................... 32
Max keycode.................................. 255
Min keycode.................................. 8
Number of pixmap formats..................... 7
Number of screens............................ 1

Pixmap formats:
  * depth =  1, bits per pixel =  1, scanline pad = 32
  * depth =  4, bits per pixel =  8, scanline pad = 32
  * depth =  8, bits per pixel =  8, scanline pad = 32
  * depth = 15, bits per pixel = 16, scanline pad = 32
  * depth = 16, bits per pixel = 16, scanline pad = 32
  * depth = 24, bits per pixel = 32, scanline pad = 32
  * depth = 32, bits per pixel = 32, scanline pad = 32

Screens:
  Screen #0
    Root..................................... 0x0000079f
    Default colormap......................... 0x00000020
    White pixel.............................. 0x00ffffff
    Black pixel.............................. 0x00000000
    Current input mask....................... 0x00fa8033
      Key press.............................. yes
      Key release............................ yes
      Button press........................... no
      Button release......................... no
      Enter window........................... yes
      Leave window........................... yes
      Pointer motion......................... no
      Pointer motion hint.................... no
      Button 1 motion........................ no
      Button 2 motion........................ no
      Button 3 motion........................ no
      Button 4 motion........................ no
      Button 5 motion........................ no
      Button motion.......................... no
      Keymap state........................... no
      Exposure............................... yes
      Visibility change...................... no
      Structure notify....................... yes
      Resize redirect........................ no
      Substructure notify.................... yes
      Substructure redirect.................. yes
      Focus change........................... yes
      Property change........................ yes
      Colormap change........................ yes
      Owner grab button...................... no
    Size..................................... 1920x1080 pixels (508x285 mm)
    Installed maps........................... min = 1, max = 1
    Root visual id........................... 0x00000021
    Backing stores........................... when mapped
    Save unders.............................. no
    Root depth............................... 24
    Number of allowed depths................. 7
    Allowed depths:
      * depth = 24, number of visuals: 576
      * depth =  1, number of visuals: 0
      * depth =  4, number of visuals: 0
      * depth =  8, number of visuals: 0
      * depth = 15, number of visuals: 0
      * depth = 16, number of visuals: 0
      * depth = 32, number of visuals: 24

Font search paths:
  * /usr/share/fonts/misc
  * /usr/share/fonts/TTF
  * /usr/share/fonts/100dpi
  * /usr/share/fonts/75dpi
  * built-ins

Supported extensions: 28
  * BIG-REQUESTS............................. v2.0
  * Composite................................ v0.4
  * DAMAGE................................... v1.1
  * DOUBLE-BUFFER............................ v1.0
  * DPMS..................................... v1.2
  * DRI2..................................... v1.4
  * DRI3..................................... v1.2
  * GLX...................................... v1.4
  * Generic Event Extension.................. v1.0
  * MIT-SCREEN-SAVER......................... v1.1
  * MIT-SHM.................................. v1.2
  * Present.................................. v1.2
  * RANDR.................................... v1.6
  * RECORD................................... v1.13
  * RENDER................................... v0.11
  * SECURITY................................. v1.0
  * SHAPE.................................... v1.1
  * SYNC..................................... v3.1
  * X-Resource............................... v1.0
  * XC-MISC.................................. v1.1
  * XFIXES................................... v5.0
  * XFree86-DGA.............................. v2.0
  * XFree86-VidModeExtension................. v2.2
  * XINERAMA................................. v1.0
  * XInputExtension.......................... v2.3
  * XKEYBOARD................................ v1.0
  * XTEST.................................... v2.2
  * XVideo................................... v2.2
```

## Unknown extension version

If an extension version shows up as *unknown*, then the extension is not
supported by the current version of `xinfo`. Feel free to open a pull request to
add support for said extension.

## Licensing

This is free and unencumbered software released into the public domain. See the
[UNLICENSE](UNLICENSE) file for more information.
