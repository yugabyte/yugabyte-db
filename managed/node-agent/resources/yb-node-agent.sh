#!/usr/bin/env bash
# Copyright 2020 YugaByte, Inc. and Contributors

ROOT_DIR="/home/yugabyte"
NODE_AGENT_DIR="$ROOT_DIR/node-agent"
NODE_AGENT_PKG_DIR="$NODE_AGENT_DIR/pkg"
NODE_AGENT_RELEASE_DIR="$NODE_AGENT_DIR/release"

TYPE="$1"
VERSION="$2"

# Remove the previous symlink in case of upgrade.
if [ "$TYPE" = "upgrade" ]; then
    unlink $NODE_AGENT_PKG_DIR
fi


# Create a new symlink between node-agent/pkg -> node-agent/release/<version>
ln -s -f $NODE_AGENT_RELEASE_DIR/$VERSION $NODE_AGENT_PKG_DIR

# Prompt for configuring the yb node agent and register during installation
if [ "$TYPE" = "install" ]; then
    PLATFORM_URL="$3"
    API_TOKEN="$4"
    node-agent node configure --api_token $API_TOKEN --url $PLATFORM_URL
    if [ $? -ne 0 ]; then
        echo "Node agent installation failed."
    fi
fi
