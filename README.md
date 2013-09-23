NDNFS
=====

NDN-friendly file system (based on FUSE)

Compilation on Mac OSX 10.8 & 10.7
----------------------------------

Dependencies:

* OXSFUSE 2.5.6;
* Sqlite;
* ndn.cxx library;
* boost library;

To compile the source code:

    ./waf configure (--debug)
    ./waf

Usage
-----

Note that keychain service for NDN certs must be configured before using NDNFS!!

To run on Mac:

    mkdir /tmp/ndnfs
    ./build/ndnfs -s /tmp/ndnfs

This will mount the file system to local folder '/tmp/ndnfs/'. To unmount NDNFS, simply type:

    umount /tmp/ndnfs

Use '-f' to run in foreground and see debug info (if you compiled with --debug option):

    ./build/ndnfs -s -f /tmp/ndnfs

If '-f' is used, NDNFS is unmounted automatically when you kill 'ndnfs' process.

To specify a global prefix for all the files stored in NDNFS:

    ./build/ndnfs -s -f /tmp/ndnfs -o prefix=/ndn/ucla.edu/cs/wentao/ndnfs

In this case, the NDN Content Object name is the global prefix + absolute file path.
