/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.cloud;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.utils.Pair;
import lombok.Getter;

public class PublicCloudConstants {

  public static final String PRODUCT_FAMILY_COMPUTE_INSTANCE = "Compute Instance";
  public static final String PRODUCT_FAMILY_STORAGE = "Storage";
  public static final String PRODUCT_FAMILY_SYSTEM_OPERATION = "System Operation";
  public static final String PRODUCT_FAMILY_PROVISIONED_THROUGHPUT = "Provisioned Throughput";

  public static final String VOLUME_TYPE_PROVISIONED_IOPS = "Provisioned IOPS";
  public static final String VOLUME_API_GENERAL_PURPOSE = "General Purpose";

  public static final String VOLUME_API_NAME_GP2 = "gp2";
  public static final String VOLUME_API_NAME_GP3 = "gp3";
  public static final String VOLUME_API_NAME_IO1 = "io1";

  public static final String GROUP_EBS_IOPS = "EBS IOPS";
  public static final String GROUP_EBS_THROUGHPUT = "EBS Throughput";

  public static final String IO1_SIZE = "io1.size";
  public static final String IO1_PIOPS = "io1.piops";
  public static final String GP2_SIZE = "gp2.size";
  public static final String GP3_SIZE = "gp3.size";
  public static final String GP3_PIOPS = "gp3.piops";
  public static final String GP3_THROUGHPUT = "gp3.throughput";

  public enum Tenancy {
    Shared,
    Dedicated,
    Host
  }

  public enum OsType {
    CENTOS,
    ALMALINUX,
    DARWIN,
    LINUX,
    EL8
  }

  public enum Architecture {
    x86_64(
        "glob:**yugabyte*{centos,alma,linux,el}*x86_64.tar.gz",
        "glob:**ybc*{centos,alma,linux,el}*x86_64.tar.gz"),
    aarch64(
        "glob:**yugabyte*{centos,alma,linux,el}*aarch64.tar.gz",
        "glob:**ybc*{centos,alma,linux,el}*aarch64.tar.gz");

    private final String dbGlob;
    private final String ybcGlob;

    Architecture(String dbGlob, String ybcGlob) {
      this.dbGlob = dbGlob;
      this.ybcGlob = ybcGlob;
    }

    public String getDBGlob() {
      return dbGlob;
    }

    public String getYbcGlob() {
      return ybcGlob;
    }
  }

  /**
   * Tracks the supported storage options for each cloud provider. Options in the UI will be ordered
   * alphabetically e.g. Persistent will be the default value for GCP, not Scratch
   */
  public enum StorageType {
    IO1(Common.CloudType.aws, new Pair<>(100, 64000)),
    GP2(Common.CloudType.aws),
    GP3(Common.CloudType.aws, new Pair<>(3000, 16000), new Pair<>(125, 1000)),
    Scratch(Common.CloudType.gcp),
    Persistent(Common.CloudType.gcp),
    StandardSSD_LRS(Common.CloudType.azu),
    Premium_LRS(Common.CloudType.azu),
    PremiumV2_LRS(Common.CloudType.azu, new Pair<>(3000, 80_000), new Pair<>(1, 1200)),
    UltraSSD_LRS(Common.CloudType.azu, new Pair<>(100, 160_000), new Pair<>(1, 3814)),
    Local(Common.CloudType.local);

    @Getter private final Common.CloudType cloudType;
    @Getter private final Pair<Integer, Integer> iopsRange;
    @Getter private final Pair<Integer, Integer> throughputRange;

    StorageType(Common.CloudType cloudType) {
      this(cloudType, null /* iopsRange */);
    }

    StorageType(Common.CloudType cloudType, Pair<Integer, Integer> iopsRange) {
      this(cloudType, iopsRange, null /* throughputRange */);
    }

    StorageType(
        Common.CloudType cloudType,
        Pair<Integer, Integer> iopsRange,
        Pair<Integer, Integer> throughputRange) {
      this.cloudType = cloudType;
      this.iopsRange = iopsRange;
      this.throughputRange = throughputRange;
    }

    public boolean isIopsProvisioning() {
      return iopsRange != null;
    }

    public boolean isThroughputProvisioning() {
      return throughputRange != null;
    }
  }
}
