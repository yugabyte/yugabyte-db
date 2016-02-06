#!/bin/bash

packages=(
  automake
  autotools-dev
  libboost-dev
  libboost-system-dev
  libboost-thread-dev
  liboauth-dev
  libsasl2-dev
  libtool
)
sudo apt-get install -y ${packages[@]}
