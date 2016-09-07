// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

public class AWSConstants {
  public static final String providerCode = "aws";

  public enum Tenancy {
    Shared,
    Dedicated,
    Host
  }
}
