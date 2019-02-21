// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;
import com.yugabyte.yw.commissioner.Common;

public class PublicCloudConstants {
  public static final String IO1_SIZE = "io1.size";
  public static final String IO1_PIOPS = "io1.piops";
  public static final String GP2_SIZE = "gp2.size";
	public enum Tenancy {
    Shared,
    Dedicated,
    Host
  }

  /**
   * Tracks the supported storage options for each cloud provider.
   * Options in the UI will be ordered alphabetically
   * e.g. Persistent will be the default value for GCP, not Scratch
   */
  public enum StorageType {
    IO1(Common.CloudType.aws),
    GP2(Common.CloudType.aws),
    Scratch(Common.CloudType.gcp),
    Persistent(Common.CloudType.gcp);

    private Common.CloudType cloudType;

    StorageType(Common.CloudType cloudType) {
      this.cloudType = cloudType;
    }

    public Common.CloudType getCloudType() {
      return cloudType;
    }
  }

}
