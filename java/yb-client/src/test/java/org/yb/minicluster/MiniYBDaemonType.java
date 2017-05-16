/**
 * Copyright (c) YugaByte, Inc.
 */
package org.yb.minicluster;

public enum MiniYBDaemonType {
  MASTER {
    @Override
    public String shortStr() { return "m"; }
  },
  TSERVER {
    @Override
    public String shortStr() { return "ts"; }
  };

  abstract String shortStr();
}
