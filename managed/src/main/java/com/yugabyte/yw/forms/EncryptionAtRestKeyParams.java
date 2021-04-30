/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1
 * .0.0.txt
 */

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.KeyType;
import com.yugabyte.yw.forms.UniverseTaskParams.EncryptionAtRestConfig.OpType;

import java.util.UUID;

import static play.mvc.Http.Status.BAD_REQUEST;

public class EncryptionAtRestKeyParams extends UniverseTaskParams {
  public static OpType opTypeFromString(String opTypeString) {
    return opTypeString == null ? OpType.UNDEFINED : OpType.valueOf(opTypeString);
  }

  public static EncryptionAtRestKeyParams bindFromFormData(
    UUID universeUUID, ObjectNode formData) {
    EncryptionAtRestKeyParams params = new EncryptionAtRestKeyParams();
    if (formData.get("kmsConfigUUID") != null) {
      params.encryptionAtRestConfig.kmsConfigUUID =
        UUID.fromString(formData.get("kmsConfigUUID").asText());
      if (formData.get("key_type") != null) {
        params.encryptionAtRestConfig.type =
          Enum.valueOf(KeyType.class, formData.get("key_type").asText());
      }
      if (formData.get("key_op") != null) {
        params.encryptionAtRestConfig.opType =
          opTypeFromString(formData.get("key_op").asText());
      }
    } else {
      throw new YWServiceException(BAD_REQUEST, "kmsConfigUUID is a required field");
    }
    params.universeUUID = universeUUID;
    return params;
  }
}
