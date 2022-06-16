#!/usr/bin/env bash

export NPM_BIN=`npm bin -g 2>/dev/null`

# Install openapi-cli
if [ ! -f $NPM_BIN/openapi ];
then
  echo "Installing openapi-cli npm package"
  npm install -g "@redocly/openapi-cli"
else
  echo "openapi-cli package already installed"
fi

