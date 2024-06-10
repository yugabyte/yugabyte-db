/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.KeyType;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;

@ApiModel(description = "Encryption at rest configuration")
public class EncryptionAtRestConfig {

  public enum OpType {
    @EnumValue("ENABLE")
    ENABLE,
    @EnumValue("DISABLE")
    DISABLE,
    @EnumValue("UNDEFINED")
    UNDEFINED;
  }

  // Whether a universe is currently encrypted at rest or not
  @ApiModelProperty(value = "Whether a universe is currently encrypted at rest")
  public boolean encryptionAtRestEnabled;

  // The KMS Configuration associated with the encryption keys being used on this universe
  @JsonAlias({"configUUID"})
  @ApiModelProperty(value = "KMS configuration UUID")
  public UUID kmsConfigUUID;

  // Whether to enable/disable/rotate universe key/encryption at rest
  @JsonAlias({"key_op"})
  @ApiModelProperty(
      value = "Operation type: enable, disable, or rotate the universe key/encryption at rest")
  public OpType opType;

  // Whether to generate a data key or just retrieve the CMK arn
  @JsonAlias({"key_type"})
  @ApiModelProperty(value = "Whether to generate a data key or just retrieve the CMK ARN")
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
