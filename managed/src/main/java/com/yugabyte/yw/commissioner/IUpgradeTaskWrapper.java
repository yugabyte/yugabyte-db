// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

@FunctionalInterface
public interface IUpgradeTaskWrapper {
  void run();
}
