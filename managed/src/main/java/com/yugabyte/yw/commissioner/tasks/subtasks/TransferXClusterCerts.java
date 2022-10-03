/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import java.io.File;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class TransferXClusterCerts extends NodeTaskBase {

  @Inject
  protected TransferXClusterCerts(
      BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // The path to the source root certificate on the Platform host.
    public File rootCertPath;
    // The replication group name used in the coreDB. It must have
    // <srcUniverseUuid>_<configName> format.
    public String replicationGroupName;
    // The target universe will look into this directory for source root certificates.
    public File producerCertsDirOnTarget;
    // Whether ignore errors while doing transfer cert operation.
    public boolean ignoreErrors;

    public enum Action {
      // Transfer the certificate to the node.
      COPY,
      // Remove the certificate from the node.
      REMOVE;

      @Override
      public String toString() {
        return name().toLowerCase();
      }
    }

    public Action action;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(action=%s,replicationGroupName=%s,rootCertPath=%s,producerCertsDirOnTarget=%s,"
            + "ignoreErrors=%b)",
        super.getName(),
        taskParams().action,
        taskParams().replicationGroupName,
        taskParams().rootCertPath,
        taskParams().producerCertsDirOnTarget,
        taskParams().ignoreErrors);
  }

  @Override
  public void run() {
    log.info("Running {} against node {}", getName(), taskParams().nodeName);

    try {
      // Check that task parameters are valid.
      if (taskParams().action == Params.Action.COPY && taskParams().rootCertPath == null) {
        throw new IllegalArgumentException("taskParams().rootCertPath must not be null");
      }
      if (taskParams().action == Params.Action.COPY && !taskParams().rootCertPath.exists()) {
        throw new IllegalArgumentException(
            String.format("file \"%s\" does not exist", taskParams().rootCertPath));
      }

      if (taskParams().producerCertsDirOnTarget == null) {
        throw new IllegalArgumentException(
            "taskParams().producerCertsDirOnTarget must not be null");
      }

      if (StringUtils.isBlank(taskParams().replicationGroupName)) {
        throw new IllegalArgumentException("taskParams().replicationConfigName must have a value");
      }

      getNodeManager()
          .nodeCommand(NodeCommandType.Transfer_XCluster_Certs, taskParams())
          .processErrors();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      if (!taskParams().ignoreErrors) {
        throw new RuntimeException(e);
      } else {
        log.debug("Error ignored because `ignoreErrors` is true");
      }
    }

    log.info("Completed {} against node {}", getName(), taskParams().nodeName);
  }
}
