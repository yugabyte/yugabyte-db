// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.avaje.ebean.annotation.EnumValue;

import java.util.UUID;
import java.util.Map;
import java.util.Set;

import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.KeyType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.DeviceInfo;

public class UniverseTaskParams extends AbstractTaskParams {
  public static class EncryptionAtRestConfig {
    public enum OpType {
      @EnumValue("ENABLE")
      ENABLE,
      @EnumValue("DISABLE")
      DISABLE,
      @EnumValue("UNDEFINED")
      UNDEFINED;
    }

    // Whether a universe is currently encrypted at rest or not
    public boolean encryptionAtRestEnabled;

    // The KMS Configuration associated with the encryption keys being used on this universe
    public UUID kmsConfigUUID;

    // Whether to enable/disable/rotate universe key/encryption at rest
    public OpType opType;

    // Whether to generate a data key or just retrieve the CMK arn
    public KeyType type;

    public EncryptionAtRestConfig() {
      this.encryptionAtRestEnabled = false;
      this.kmsConfigUUID = null;
      this.type = KeyType.DATA_KEY;
      this.opType = OpType.UNDEFINED;
    }

    public EncryptionAtRestConfig(EncryptionAtRestConfig config) {
      this.encryptionAtRestEnabled = config.encryptionAtRestEnabled;
      this.kmsConfigUUID = config.kmsConfigUUID;
      this.type = config.type;
      this.opType = config.opType;
    }

    public EncryptionAtRestConfig clone() {
      return new EncryptionAtRestConfig(this);
    }
  }

  // The primary device info.
  public DeviceInfo deviceInfo;

  // The universe against which this operation is being executed.
  public UUID universeUUID;

  // Expected version of the universe for operation execution. Set to -1 if an operation should
  // not verify expected version of the universe.
  public int expectedUniverseVersion;

  // If an AWS backed universe has chosen EBS volume encryption, this will be set to the
  // Amazon Resource Name (ARN) of the CMK to be used to generate data keys for volume encryption
  public String cmkArn;

  // Store encryption key provider specific configuration/authorization values
  public EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();

  // The set of nodes that are part of this universe. Should contain nodes in both primary and
  // readOnly clusters.
  public Set<NodeDetails> nodeDetailsSet = null;
}
