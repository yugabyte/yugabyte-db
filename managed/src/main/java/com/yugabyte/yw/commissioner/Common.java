// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

public class Common {
  // The various cloud types supported.
  public enum CloudType {
    unknown,
    aws,
    gcp,
    azu,
    docker,
    onprem,
    kubernetes,
    other
  }
}
