#!/bin/bash

echo "build linux version"
make || exit "build linux version failure"
make clean

echo "build windows version"
mingw32-make -f Makefile.mingw32
mingw32-make clean

