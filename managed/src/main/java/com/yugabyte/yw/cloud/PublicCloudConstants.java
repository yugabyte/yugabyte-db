/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.cloud;

import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.DeserializationContext;
import com.fasterxml.jackson.databind.JsonDeserializer;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.utils.Pair;
import java.io.IOException;
import java.util.EnumSet;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;

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
  public static final String VOLUME_API_NAME_IO2 = "io2";

  public static final String GROUP_EBS_IOPS = "EBS IOPS";
  public static final String GROUP_EBS_THROUGHPUT = "EBS Throughput";

  public static final String IO1_SIZE = "io1.size";
  public static final String IO1_PIOPS = "io1.piops";
  public static final String IO2_SIZE = "io2.size";
  public static final String IO2_PIOPS = "io2.piops";
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

    public static Architecture parse(String strType) {
      for (Architecture arch : EnumSet.allOf(Architecture.class)) {
        if (arch.name().equalsIgnoreCase(strType)) {
          return arch;
        }
      }
      throw new IllegalArgumentException("Unknown architecture: " + strType);
    }

    @Slf4j
    public static class JSONDeserializer extends JsonDeserializer<Architecture> {
      @Override
      public Architecture deserialize(JsonParser p, DeserializationContext ctxt)
          throws IOException {
        String text = p.getText();
        log.trace("Deserializing architecture: {}", text);
        if (text == null || text.isEmpty()) {
          return null;
        }
        return parse(text);
      }
    }
  }

  /**
   * Tracks the supported storage options for each cloud provider. Options in the UI will be ordered
   * alphabetically e.g. Persistent will be the default value for GCP, not Scratch
   */
  public enum StorageType {
    IO1(Common.CloudType.aws, new Pair<>(100, 64000)),
    IO2(Common.CloudType.aws, new Pair<>(100, 256000)),
    GP2(Common.CloudType.aws),
    GP3(Common.CloudType.aws, new Pair<>(3000, 16000), new Pair<>(125, 1000)),
    Scratch(Common.CloudType.gcp),
    Persistent(Common.CloudType.gcp),
    Hyperdisk_Balanced(Common.CloudType.gcp, new Pair<>(3000, 160000), new Pair<>(250, 2400)),
    Hyperdisk_Extreme(Common.CloudType.gcp, new Pair<>(2, 350000), null),
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
