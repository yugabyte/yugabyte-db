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

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.nio.file.Paths;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class TransferXClusterCerts extends NodeTaskBase {

  public static final String K8S_TLS_SUPPORT_CONFIG_KEY = "yb.xcluster.k8s_tls_support";

  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  protected TransferXClusterCerts(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // The universe UUID of where the certificate will be copied to must be stored in universeUUID
    // field.
    // The name of the node to copy the certificate to must be stored in nodeName field.
    // The path to the root certificate to copy on the Platform host.
    public File rootCertPath;
    // The replication group name used in the coreDB. It must have
    // <srcUniverseUuid>_<configName> format.
    public String replicationGroupName;
    // The destination universe will look into this directory for origin univere's root
    // certificates.
    public File destinationCertsDir;
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
        "%s(action=%s,replicationGroupName=%s,rootCertPath=%s,destinationCertsDir=%s,"
            + "ignoreErrors=%b)",
        super.getName(),
        taskParams().action,
        taskParams().replicationGroupName,
        taskParams().rootCertPath,
        taskParams().destinationCertsDir,
        taskParams().ignoreErrors);
  }

  @Override
  public void run() {
    log.info("Running {} against node {}", getName(), taskParams().nodeName);

    try {
      // Check that task parameters are valid.
      if (taskParams().action == Params.Action.COPY) {
        if (taskParams().rootCertPath == null) {
          throw new IllegalArgumentException("taskParams().rootCertPath must not be null");
        }

        if (!taskParams().rootCertPath.exists()) {
          throw new IllegalArgumentException(
              String.format("file \"%s\" does not exist", taskParams().rootCertPath));
        }
      }

      if (taskParams().destinationCertsDir == null) {
        throw new IllegalArgumentException("taskParams().destinationCertsDir must not be null");
      }

      if (StringUtils.isBlank(taskParams().replicationGroupName)) {
        throw new IllegalArgumentException("taskParams().replicationConfigName must have a value");
      }

      if (config.getBoolean(K8S_TLS_SUPPORT_CONFIG_KEY)) {
        transferXClusterCertUsingNodeUniverseManager();
      } else {
        getNodeManager()
            .nodeCommand(NodeCommandType.Transfer_XCluster_Certs, taskParams())
            .processErrors();
      }
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

  private void transferXClusterCertUsingNodeUniverseManager() {
    // Find the specified universe and node.
    Optional<Universe> destinationUniverseOptional =
        Universe.maybeGet(taskParams().getUniverseUUID());
    if (destinationUniverseOptional.isEmpty()) {
      throw new IllegalArgumentException(
          String.format("No universe with UUID %s found", taskParams().getUniverseUUID()));
    }
    Universe destinationUniverse = destinationUniverseOptional.get();
    NodeDetails node = destinationUniverse.getNode(taskParams().nodeName);
    if (node == null) {
      throw new IllegalArgumentException(
          String.format(
              "Node with name %s in universe %s not found",
              taskParams().nodeName, taskParams().getUniverseUUID()));
    }

    String destinationCertsDirPath =
        Paths.get(taskParams().destinationCertsDir.toString(), taskParams().replicationGroupName)
            .toString();
    String destinationCertPath =
        Paths.get(destinationCertsDirPath, XClusterConfigTaskBase.XCLUSTER_ROOT_CERTIFICATE_NAME)
            .toString();
    if (taskParams().action == Params.Action.COPY) {
      log.info(
          "Moving server cert located at {} to {}:{} in universe {}",
          taskParams().rootCertPath,
          taskParams().nodeName,
          destinationCertPath,
          taskParams().getUniverseUUID());

      // Create the parent directory for the certificate file.
      nodeUniverseManager
          .runCommand(
              node, destinationUniverse, ImmutableList.of("mkdir", "-p", destinationCertsDirPath))
          .processErrors("Making certificate parent directory failed");

      // The permission for the certs used to be set to `400` which could be problematic in the case
      // that we want to overwrite the certificate.
      if (destinationUniverse.getUniverseDetails().getPrimaryCluster().userIntent.providerType
          != CloudType.kubernetes) {
        nodeUniverseManager
            .runCommand(
                node,
                destinationUniverse,
                ImmutableList.of(
                    "find",
                    destinationCertsDirPath,
                    "-type",
                    "f",
                    "-exec",
                    "chmod",
                    "600",
                    "'{}'",
                    "\\;"))
            .processErrors("Changing the certificates' permission failed");
      }

      // Copy the certificate file to the node.
      nodeUniverseManager.uploadFileToNode(
          node,
          destinationUniverse,
          taskParams().rootCertPath.toString(),
          destinationCertPath,
          "600");
    } else if (taskParams().action.equals(Params.Action.REMOVE)) {
      log.info(
          "Removing server cert located at {} from node {} in universe {}",
          destinationCertPath,
          taskParams().nodeName,
          taskParams().getUniverseUUID());

      // Remove the certificate file.
      verifyRmCommandShellResponse(
          nodeUniverseManager.runCommand(
              node, destinationUniverse, ImmutableList.of("rm", destinationCertPath)));

      // Remove the directory only if it is empty.
      verifyRmCommandShellResponse(
          nodeUniverseManager.runCommand(
              node, destinationUniverse, ImmutableList.of("rm", "-d", destinationCertsDirPath)));

      // Remove the directory only if it is empty. No need to check whether it succeeded because
      // this directory should be deleted only if there are no other xCluster configs.
      nodeUniverseManager.runCommand(
          node,
          destinationUniverse,
          ImmutableList.of("rm", "-d", taskParams().destinationCertsDir.toString()));
    } else {
      throw new IllegalArgumentException(String.format("Action %s not found", taskParams().action));
    }
  }

  private void verifyRmCommandShellResponse(ShellResponse response) {
    log.debug("Output is {}", response.getMessage());
    if (response.getCode() != 0 && !response.getMessage().contains("No such file or directory")) {
      response.processErrors("Command 'rm' failed");
    }
  }
}
