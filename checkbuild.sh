#!/bin/bash

make clean

echo "build linux version"
make || exit "build linux version failure"
make clean

echo "build windows version"
MING_MAKE=$(which mingw32-make)
if [ X${MING_MAKE} = X"" ]; then
    make CC=i586-mingw32msvc-cc CXX=i586-mingw32msvc-c++
    make clean
else
    mingw32-make
    mingw32-make clean
fi

