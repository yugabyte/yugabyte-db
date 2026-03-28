#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

LLVM_VERSION=20

wget https://apt.llvm.org/llvm.sh
chmod +x ./llvm.sh
sudo ./llvm.sh $LLVM_VERSION all
sudo ln -s /usr/lib/llvm-$LLVM_VERSION/bin/clang-cl /usr/bin/clang-cl
sudo ln -s /usr/lib/llvm-$LLVM_VERSION/bin/llvm-lib /usr/bin/llvm-lib
sudo ln -s /usr/lib/llvm-$LLVM_VERSION/bin/lld-link /usr/bin/lld-link
sudo ln -s /usr/lib/llvm-$LLVM_VERSION/bin/llvm-ml /usr/bin/llvm-ml
sudo ln -s /usr/lib/llvm-$LLVM_VERSION/bin/ld.lld /usr/bin/ld.lld
sudo ln -s /usr/lib/llvm-$LLVM_VERSION/bin/clang /usr/bin/clang