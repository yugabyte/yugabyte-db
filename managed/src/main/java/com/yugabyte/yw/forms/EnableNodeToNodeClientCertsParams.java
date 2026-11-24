// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.RpcClientCert;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;

/**
 * Turns on useNodeToNodeClientCerts for an existing universe. Non-rolling only: one DB gflag both
 * sends and requires the client cert, so during a roll a restarted server would refuse the peers
 * that haven't restarted yet.
 */
@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = EnableNodeToNodeClientCertsParams.Converter.class)
public class EnableNodeToNodeClientCertsParams extends UpgradeTaskParams {

  public EnableNodeToNodeClientCertsParams() {
    upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
  }

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    if (upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Client certificates can only be enabled with a non-rolling upgrade.");
    }
    // A retry runs after the setting was saved, so only the first try checks it.
    if (!isFirstTry) {
      return;
    }
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    if (RpcClientCert.isClientCertRequired(details)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Universe " + universe.getName() + " already uses client certificates.");
    }
    // A rollback could go below the version floor that validateSupported checks.
    if (details.softwareUpgradeState != SoftwareUpgradeState.Ready) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot enable client certificates in software upgrade state %s. Finalize or roll"
                  + " back the software upgrade first.",
              details.softwareUpgradeState));
    }
    RpcClientCert.validateSupported(details);
    for (Cluster cluster : details.clusters) {
      GFlagsUtil.validateNodeToNodeClientCerts(cluster.userIntent, true);
    }
    // The servers' xCluster clients build their secure context without a node name, which fails
    // when node_to_node_encryption_use_client_certificates is on.
    if (!XClusterConfig.getByUniverseUuid(universe.getUniverseUUID()).isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Client certificates can't be enabled on a universe in an xCluster or DR config.");
    }
  }

  public static class Converter extends BaseConverter<EnableNodeToNodeClientCertsParams> {}
}
