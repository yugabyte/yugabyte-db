#!/bin/bash

packages=(
  automake
  libtool
)
sudo apt-get install -y ${packages[@]}
