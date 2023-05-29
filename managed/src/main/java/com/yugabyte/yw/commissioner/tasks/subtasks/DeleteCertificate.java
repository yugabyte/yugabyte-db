/*
 Copyright 2019 YugaByte, Inc. and Contributors

  Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
  may not use this file except in compliance with the License. You
  may obtain a copy of the License at

 https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
*/

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.CertificateInfo;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteCertificate extends UniverseTaskBase {

  @Inject
  protected DeleteCertificate(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for certificate deletion.
  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
    public UUID certUUID;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      if (taskParams().certUUID == null) return;
      CertificateInfo cert = CertificateInfo.get(taskParams().certUUID);
      if (cert == null) return;
      if (cert.getInUse()) {
        log.info("Certificate already in use, can't delete.");
        return;
      }

      CertificateInfo.delete(taskParams().certUUID, taskParams().customerUUID);
    } catch (Throwable t) {
      throw t;
    }
  }
}
