pixz
====

[![Build Status](https://travis-ci.org/vasi/pixz.svg?branch=master)](https://travis-ci.org/vasi/pixz)

Pixz (pronounced *pixie*) is a parallel, indexing version of `xz`.

Repository: https://github.com/vasi/pixz

Downloads: https://github.com/vasi/pixz/releases

pixz vs xz
----------

The existing [XZ Utils](http://tukaani.org/xz/) provide great compression in the `.xz` file format,
but they produce just one big block of compressed data. Pixz instead produces a collection of
smaller blocks which makes random access to the original data possible. This is especially useful
for large tarballs.

### Differences to xz

-   `pixz` automatically indexes tarballs during compression (unless the `-t` argument is used)
-   `pixz` supports parallel decompression, which `xz` does not
-   `pixz` defaults to using all available CPU cores, while `xz` defaults to using only one core
-   `pixz` provides `-i` and `-o` command line options to specify input and output file
-   `pixz` does not need the command line option `-z` (or `--compress`). Instead, it compresses by default, and decompresses if `-d` is passed.
-   `pixz` uses different logic to decide whether to use stdin/stdout. `pixz somefile` will always output to another file, while `pixz` with no filenames will always use stdin/stdout. There's no `-c` argument to explicitly request stdout.
-   Some other flags mean different things for `pixz` and `xz`, including `-f`, `-l`, `-q` and `-t`. Please read the manpages for more detail on these.

Building pixz
-------------

General help about the building process's configuration step can be acquired via:

```
./configure --help
```

### Dependencies

-   pthreads
-   liblzma 4.999.9-beta-212 or later (from the xz distribution)
-   libarchive 2.8 or later
-   AsciiDoc to generate the man page

### Build from Release Tarball

```
./configure
make
make install
```

You many need `sudo` permissions to run `make install`.

### Build from GitHub

```
git clone https://github.com/vasi/pixz.git
cd pixz
./autogen.sh
./configure
make
make install
```

You many need `sudo` permissions to run `make install`.

Usage
-----

### Single Files

Compress a single file (no tarball, just compression), multi-core:

    pixz bar bar.xz

Decompress it, multi-core:

    pixz -d bar.xz bar

### Tarballs

Compress and index a tarball, multi-core:

    pixz foo.tar foo.tpxz

Very quickly list the contents of the compressed tarball:

    pixz -l foo.tpxz

Decompress the tarball, multi-core:

    pixz -d foo.tpxz foo.tar

Very quickly extract a single file, multi-core, also verifies that contents match index:

    pixz -x dir/file < foo.tpxz | tar x

Create a tarball using pixz for multi-core compression:

    tar -Ipixz -cf foo.tpxz foo/

### Specifying Input and Output

These are the same (also work for `-x`, `-d` and `-l` as well):

    pixz foo.tar foo.tpxz
    pixz < foo.tar > foo.tpxz
    pixz -i foo.tar -o foo.tpxz

Extract the files from `foo.tpxz` into `foo.tar`:

    pixz -x -i foo.tpxz -o foo.tar file1 file2 ...

Compress to `foo.tpxz`, removing the original:

    pixz foo.tar

Extract to `foo.tar`, removing the original:

    pixz -d foo.tpxz

### Other Flags

Faster, worse compression:

    pixz -1 foo.tar

Better, slower compression:

    pixz -9 foo.tar

Use exactly 2 threads:

    pixz -p 2 foo.tar

Compress, but do not treat it as a tarball, i.e. do not index it:

    pixz -t foo.tar

Decompress, but do not check that contents match index:

    pixz -d -t foo.tpxz

List the xz blocks instead of files:

    pixz -l -t foo.tpxz

For even more tuning flags, check the manual page:

    man pixz

Comparison to other Tools
-------------------------

### plzip

-   about equally complex and efficient
-   lzip format seems less-used
-   version 1 is theoretically indexable, I think

### ChopZip

-   written in Python, much simpler
-   more flexible, supports arbitrary compression programs
-   uses streams instead of blocks, not indexable
-   splits input and then combines output, much higher disk usage

### pxz

-   simpler code
-   uses OpenMP instead of pthreads
-   uses streams instead of blocks, not indexable
-   uses temporary files and does not combine them until the whole file is compressed, high disk and
    memory usage

### pbzip2

-   not indexable
-   appears slow
-   bzip2 algorithm is non-ideal

### pigz

-   not indexable

### dictzip, idzip

-   not parallel
