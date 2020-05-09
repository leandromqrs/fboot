## INTRODUCTION

The e4head reduces disk access times through physical file reallocation. It is based
on the online defragmentation ioctl EXT4_IOC_MOVE_EXT from the ext4 filesystem,
which was introduced in Linux Kernel 2.6.31. Therefore, other filesystem types
or earlier versions of extended filesystems are not supported.

## DEPENDENCIES

The e4rat toolset has the following external dependencies:
 - Linux Kernel (>= 2.6.31)
 - CMake (>= 2.6)
 - pod2man
 - Boost Library (>=1.41): You need the following components installed:
       system, filesytem, regex, signals2
 - Linux Audit Library (libaudit >=0.1.7)
 - Ext2 File System Utilities (e2fsprogs)

## BUILDING

The build system is based on CMake, which will generate a Makefile.
To build the release version of e4rat run the following command:
    
    $ mkdir build
    $ cd build
    $ cmake .. -DCMAKE_BUILD_TYPE=release
    $ make

Additianal cmake options:
 CMAKE_INSTALL_PREFIX=<prefix>
 
Once you have successfully built the source code install e4rat as root:

    $ sudo make install
    
If you are running a Debian based Linux you can also install e4rat by
generating a Debian package:
    make package
    dpkg -i e4rat_<version>_<arch>.deb

## ADDITIONAL CMAKE OPTIONS:

CMAKE_INSTALL_PREFIX=<prefix>
    install files in <prefix>. When you run make install, libraries will
    be placed in <prefix>/lib, executables in <prefix>/bin, and so on.

    The default is / if this argument is not passed to cmake.

BUILD_CORE_LIBRARY_STATIC=<true|false>
    set to 'true' to build libe4rat-core library statically. Please not 
    that linking statically increase the file size.

    On 64-bit systems the default is set to 'true' otherwise 'false'.

CMAKE_BUILD_TYPE=<release|debug>
    specify build type. Choose either 'debug' or 'release'. The debug
    version is build with debug info.

    The default is 'debug' if this argument is not passed to cmake.

## AUTHORS

e4rat has been developed by Andreas Rid <conso at users.sf.net> under the
guidance of Gundolf Kiefer <gundolf.kiefer@hs-augsburg.de> at
the University of Applied Sciences, Augsburg.
