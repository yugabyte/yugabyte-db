#!/bin/sh

# Create deps folder
mkdir deps
cd deps

# Download GoogleTest
wget https://googletest.googlecode.com/files/gtest-1.7.0.zip
unzip gtest-1.7.0.zip
cd gtest-1.7.0

# Build GoogleTest
mkdir build
cd build
cmake ..
make -j
cd ../../

# Install GoogleTest
mkdir -p gtest/lib
mv gtest-1.7.0/include gtest/
mv gtest-1.7.0/build/*.a gtest/lib
