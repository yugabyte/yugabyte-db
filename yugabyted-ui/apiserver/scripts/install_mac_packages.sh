#!/usr/bin/env bash

#for cmd in yq jq; do
for cmd in yq; do
  command -v $cmd >/dev/null 2>&1

  # Install if not present.
  if [ $? -ne 0 ];
  then
    echo "Installing $cmd package"
    brew install $cmd
  else
    echo "$cmd package already installed"
  fi
done
