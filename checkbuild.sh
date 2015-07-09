#!/bin/bash

make clean

echo "build linux version"
make || exit "build linux version failure"
make clean

echo "build windows version"
MING_MAKE=$(which mingw32-make)
if [ X${MING_MAKE} = X"" ]; then
	make CC=i686-w64-mingw32-gcc CXX=i686-w64-mingw32-g++
    make clean
else
    mingw32-make
    mingw32-make clean
fi

