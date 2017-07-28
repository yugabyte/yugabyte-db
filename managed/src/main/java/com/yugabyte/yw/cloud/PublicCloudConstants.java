// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

public class PublicCloudConstants {
  public static final String IO1_SIZE = "io1.size";
  public static final String IO1_PIOPS = "io1.piops";
  public static final String GP2_SIZE = "gp2.size";
	public enum Tenancy {
    Shared,
    Dedicated,
    Host
  }
}
