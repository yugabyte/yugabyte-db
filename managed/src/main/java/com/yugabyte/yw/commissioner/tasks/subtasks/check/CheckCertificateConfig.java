// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.NodeTaskBase;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckCertificateConfig extends NodeTaskBase {

  private static final String runtimeConfigKey = "yb.tls.skip_cert_validation";

  @Inject
  protected CheckCertificateConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public boolean SkipHostNameCheck = false;
  }

  protected CheckCertificateConfig.Params taskParams() {
    return (CheckCertificateConfig.Params) taskParams;
  }

  @Override
  public void run() {
    try {
      log.info("Running CheckCertificateConfig task for node {}.", taskParams().nodeName);
      ShellResponse response =
          getNodeManager().nodeCommand(NodeManager.NodeCommandType.Verify_Certs, taskParams());
      log.info("Response: " + response.message);
      if (response.code != 0) {
        log.error(
            "Failed to verify certificates for node {}:\n{}",
            taskParams().nodeName,
            response.message);
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, response.message);
      }
    } catch (Exception e) {
      log.error(
          "Validation failed, please check the certificate configuration. Alternatively you can"
              + " set "
              + runtimeConfigKey
              + " to ALL skip it completely and retry the"
              + " task.");
      Throwables.propagate(e);
    }
  }
}
