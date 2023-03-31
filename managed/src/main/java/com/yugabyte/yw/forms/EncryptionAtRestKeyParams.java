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

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.yugabyte.yw.common.PlatformServiceException;
import java.util.UUID;
import play.libs.Json;
import play.mvc.Http;

public class EncryptionAtRestKeyParams extends UniverseTaskParams {

  public static EncryptionAtRestKeyParams bindFromFormData(
      UUID universeUUID, Http.Request request) {
    EncryptionAtRestKeyParams taskParams = new EncryptionAtRestKeyParams();
    taskParams.setUniverseUUID(universeUUID);
    try {
      taskParams.encryptionAtRestConfig =
          Json.mapper().treeToValue(request.body().asJson(), EncryptionAtRestConfig.class);
    } catch (JsonProcessingException e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
    return taskParams;
  }
}
