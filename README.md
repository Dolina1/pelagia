# pelagia ![C Ubuntu](https://github.com/surparallel/pelagia/workflows/C%20Ubuntu/badge.svg) ![C Macos](https://github.com/surparallel/pelagia/workflows/C%20Macos/badge.svg)

## Introduce

Pelagia is an automatic parallelization tool (lock-free) developed by surparallel open source based on the concept of sequential virtual machine.
Pelagia is developed by ANSI C, abides by AGPL protocol, and supports multiple operating systems and languages.
It supports automatic parallelization, transparent thread calling, embedded key value database, embedded multi language running environment, and provides API and documents for each language.
The set concept in Boolean algebra is used to describe the operation of data. Including key, value, ordered set, index set and other types.

## principle

Pelagia slices the attribute data according to the ECS method. According to the property read and write different automatically allocated to different threads to improve the efficiency.Pelagia is very impressed with writing data. All the table data written to the order must be written through "PLG mngaddtable".To use the following methods in order, you need to set write permissions. Otherwise, the write will fail.

*plg_JobSet
*plg_JobMultiSet
*plg_JobDel
*plg_JobSetIfNoExit
*plg_JobTableClear
*plg_JobRename

*plg_JobSAdd
*plg_JobSMove
*plg_JobSPop
*plg_JobSDel
*plg_JobSUionStore
*plg_JobSInterStore
*plg_JobSDiffStore


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

## Using Pelagia as a control and Dao framework on the client
<a href="https://surparallel.org"><img src="https://github.com/surparallel/pelagia_websit/blob/master/img/view_control_dao.jpg"></a>