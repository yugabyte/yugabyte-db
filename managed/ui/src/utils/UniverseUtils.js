// Copyright (c) YugaByte, Inc.

export function isNodeRemovable(nodeState) {
  return nodeState === "ToBeAdded";
}
