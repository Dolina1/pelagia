# pelagia ![C Ubuntu](https://github.com/surparallel/pelagia/workflows/C%20Ubuntu/badge.svg) ![C Macos](https://github.com/surparallel/pelagia/workflows/C%20Macos/badge.svg)

## Introduce

Pelagia is an automatic parallelization tool (lock-free) developed by surparallel open source based on the concept of sequential virtual machine.
Pelagia is developed by ANSI C, abides by AGPL protocol, and supports multiple operating systems and languages.
It supports automatic parallelization, transparent thread calling, embedded key value database, embedded multi language running environment, and provides API and documents for each language.
The set concept in Boolean algebra is used to describe the operation of data. Including key, value, ordered set, index set and other types.

## Related resources

Pelagia website: https://surparallel.org

## Example

You can find the relevant sample code in psimple.c, profesa.c under the source code.

## Environmental installation

## Install on Linux system

Linux&Mac installation is very simple, just download the source package and decompress and compile it on the terminal. This article uses version 0.1 for installation:

    git clone https://github.com/surparallel/pelagia.git
    cd pelagia/src
    make linux
    
##  Install on Mac OS X

    git clone https://github.com/surparallel/pelagia.git
    cd pelagia/src
    make macosx
    
## Install on window system

After decompression, enter the msvcs directory and open pelagia.sln for compilation.

