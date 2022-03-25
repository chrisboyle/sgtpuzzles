#!/usr/bin/env python3

# Helper script used by the NestedVM cmake build script.
#
# Usage:   glob-symlinks.py <srcdir> <wildcard> [<srcdir> <wildcard> ...]
#
# Each pair of command-line arguments is treated as a source
# directory, followed by either a single filename or a wildcard.
#
# The result is to create symlinks in the program's working directory
# mirroring all the files matched by the filenames/wildcards, each
# pointing at the appropriate source directory.
#
# For example, this command
#   glob-symlinks.py /foo \*.txt /bar wibble.blah
# might create symlinks as follows:
#   this.txt -> /foo/this.txt
#   that.txt -> /foo/that.txt
#   wibble.blah -> /bar/wibble.blah
#
# CMake could mostly do this itself, except that some of the files
# that need symlinking during the NestedVM build (to make a tree that
# we archive up into a .jar file) are Java class files with some
# '$suffix' in the name, and CMake doesn't escape the $ signs, so that
# the suffix vanishes during shell expansion.

import sys
import os
import glob

def get_arg_pairs():
    args = iter(sys.argv)
    next(args) # skip program name
    while True:
        try:
            yield next(args), next(args)
        except StopIteration:
            break

def get_globbed_pairs():
    for srcdir, pattern in get_arg_pairs():
        if glob.escape(pattern) == pattern:
            # Assume that unglobbed filenames exist
            #print("non-glob:", srcdir, pattern)
            yield srcdir, pattern
        else:
            #print("globbing:", srcdir, pattern)
            prefix = srcdir + "/"
            for filename in glob.iglob(prefix + pattern):
                assert filename.startswith(prefix)
                filename = filename[len(prefix):]
                #print("  ->", srcdir, filename)
                yield srcdir, filename

for srcdir, filename in get_globbed_pairs():
    dirname = os.path.dirname(filename)
    if len(dirname) > 0:
        try:
            os.makedirs(dirname)
        except FileExistsError:
            pass
    try:
        os.symlink(os.path.join(srcdir, filename), filename)
    except FileExistsError:
        pass
