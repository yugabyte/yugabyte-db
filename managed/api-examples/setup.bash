#!/usr/bin/bash

set -eo pipefail

if [ ! -d venv ];
then
    python3 -m venv venv
    . venv/bin/activate
    pip3 install --upgrade pip
    # see: https://anbasile.github.io/posts/2017-06-25-jupyter-venv/
    pip3 install ipykernel
    ipython kernel install --user --name=yugabyte-db-client
    pip3 install -e ~/code/yugabyte-db/managed/client/python/generated
    pip3 install pyyaml
fi
