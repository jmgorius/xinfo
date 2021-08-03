# xinfo

A tool with no external dependencies to print information about an X server
instance.

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

## Unknown extension version

If an extension version shows up as *unknown*, then the extension is not
supported by the current version of `xinfo`. Feel free to open a pull request to
add support for said extension.

## Licensing

This is free and unencumbered software released into the public domain. See the
[UNLICENSE](UNLICENSE) file for more information.
