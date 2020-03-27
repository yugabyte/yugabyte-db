// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

public class Common {
  // The various cloud types supported.
  public enum CloudType {
    unknown("unknown"),
    aws("aws"),
    gcp("gcp"),
    azu("azu"),
    docker("docker"),
    onprem("onprem"),
    kubernetes("kubernetes"),
    local("cloud-1"),
    other("other");

    private final String value;

    private CloudType(String value) {
      this.value = value;
    }

    public String toString() {
      return this.value;
    }
  }
}
