// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.certmgmt.CertManagerClientCertSync;
import com.yugabyte.yw.forms.UniverseTaskParams;
import java.time.Duration;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Copies the chart-issued client cert into YBA after helm install, so the servers, which require a
 * client cert, accept YBA's RPCs from the first WaitForServer on.
 */
@Slf4j
public class SyncCertManagerClientCert extends UniverseTaskBase {

  // cert-manager normally fills the Secret in seconds.
  private static final Duration SECRET_WAIT_TIMEOUT = Duration.ofMinutes(5);

  private final CertManagerClientCertSync certSync;

  @Inject
  protected SyncCertManagerClientCert(
      BaseTaskDependencies baseTaskDependencies, CertManagerClientCertSync certSync) {
    super(baseTaskDependencies);
    this.certSync = certSync;
  }

  public static class Params extends UniverseTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    certSync.sync(getUniverse(), SECRET_WAIT_TIMEOUT);
  }
}
