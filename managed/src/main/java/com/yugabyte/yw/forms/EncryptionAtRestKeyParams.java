/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
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
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import play.libs.Json;
import play.mvc.Http;

public class EncryptionAtRestKeyParams extends UniverseTaskParams {

  // Set when the Kubernetes operator submits the task, left null on the API path. Its presence in
  // the serialized task params is what CustomerTaskHandler.isKubernetesOperatorTask looks for to
  // report the task as run by YBA rather than by an unknown user.
  @Getter @Setter private KubernetesResourceDetails kubernetesResourceDetails;

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
