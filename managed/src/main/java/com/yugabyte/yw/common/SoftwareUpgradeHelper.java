// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.GetYsqlMajorCatalogUpgradeStateResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;
import play.mvc.Http.Status;

@Singleton
@Slf4j
public class SoftwareUpgradeHelper {

  private final YBClientService ybService;
  private final GFlagsValidation gFlagsValidation;
  private final AutoFlagUtil autoFlagUtil;

  @Inject
  public SoftwareUpgradeHelper(
      YBClientService ybService, GFlagsValidation gFlagsValidation, AutoFlagUtil autoFlagUtil) {
    this.gFlagsValidation = gFlagsValidation;
    this.ybService = ybService;
    this.autoFlagUtil = autoFlagUtil;
  }

  public YsqlMajorCatalogUpgradeState getYsqlMajorCatalogUpgradeState(Universe universe) {
    try (YBClient client =
        ybService.getClient(universe.getMasterAddresses(), universe.getCertificateClientToNode())) {
      GetYsqlMajorCatalogUpgradeStateResponse resp = client.getYsqlMajorCatalogUpgradeState();
      if (resp.hasError()) {
        log.error("Error while getting YSQL major version catalog upgrade state: ", resp);
        throw new RuntimeException(
            "Error while getting YSQL major version catalog upgrade state: "
                + resp.getServerError().toString());
      }
      return resp.getState();
    } catch (Exception e) {
      log.error("Error while getting YSQL major version catalog upgrade state: ", e);
      throw new RuntimeException(e);
    }
  }

  public boolean isYsqlMajorVersionUpgradeRequired(
      Universe universe, String oldVersion, String newVersion) {
    return gFlagsValidation.ysqlMajorVersionUpgrade(oldVersion, newVersion)
        && universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL;
  }

  public boolean isSuperUserRequiredForCatalogUpgrade(
      Universe universe, String currentVersion, String newVersion) {
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    return gFlagsValidation.ysqlMajorVersionUpgrade(currentVersion, newVersion)
        && primaryCluster.userIntent.enableYSQLAuth
        && (primaryCluster.userIntent.dedicatedNodes
            || primaryCluster.userIntent.providerType.equals(CloudType.kubernetes));
  }

  public boolean checkUpgradeRequireFinalize(String currentVersion, String newVersion) {
    try {
      return autoFlagUtil.upgradeRequireFinalize(currentVersion, newVersion);
    } catch (IOException e) {
      log.error("Error: ", e);
      throw new PlatformServiceException(
          Status.INTERNAL_SERVER_ERROR, "Error while checking auto-finalize for upgrade");
    }
  }

  public boolean isAllMasterUpgradedToYsqlMajorVersion(Universe universe, String ysqlMajorVersion) {
    try (YBClient client =
        ybService.getClient(universe.getMasterAddresses(), universe.getCertificateClientToNode())) {
      return universe.getMasters().stream()
          .allMatch(
              master -> {
                return ybService
                    .getYsqlMajorVersion(client, master.cloudInfo.private_ip, master.masterRpcPort)
                    .map(version -> version.equals(ysqlMajorVersion))
                    .orElse(false);
              });
    } catch (Exception e) {
      log.error("Error while getting YSQL major version: ", e);
      return false;
    }
  }

  public boolean isAnyMasterUpgradedOrInProgressForYsqlMajorVersion(
      Universe universe, String ysqlMajorVersion) {
    try (YBClient client =
        ybService.getClient(universe.getMasterAddresses(), universe.getCertificateClientToNode())) {
      return universe.getMasters().stream()
          .anyMatch(
              master -> {
                return ybService
                    .getYsqlMajorVersion(client, master.cloudInfo.private_ip, master.masterRpcPort)
                    .map(version -> version.equals(ysqlMajorVersion))
                    .orElse(false);
              });
    } catch (Exception e) {
      log.error("Error while getting YSQL major version: ", e);
      // If we are not able to get the version, then assume the master upgrade was in progress.
      return true;
    }
  }

  public boolean isYsqlMajorUpgradeIncomplete(Universe universe) {
    if (universe.getUniverseDetails() == null
        || universe.getUniverseDetails().getPrimaryCluster() == null
        || universe.getUniverseDetails().getPrimaryCluster().userIntent == null) {
      return false;
    }
    if (universe.getUniverseDetails().prevYBSoftwareConfig == null) {
      return false;
    }
    UserIntent primaryIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (!primaryIntent.enableYSQL) {
      return false;
    }
    PrevYBSoftwareConfig prevConfig = universe.getUniverseDetails().prevYBSoftwareConfig;
    String prevVersion =
        !StringUtils.isEmpty(prevConfig.getSoftwareVersion())
            ? prevConfig.getSoftwareVersion()
            : primaryIntent.ybSoftwareVersion;
    // Incase of failed software upgrade, prevYBSoftwareConfig will have the target version.
    String newVersion =
        !StringUtils.isEmpty(prevConfig.getTargetUpgradeSoftwareVersion())
            ? prevConfig.getTargetUpgradeSoftwareVersion()
            : primaryIntent.ybSoftwareVersion;
    return gFlagsValidation.ysqlMajorVersionUpgrade(prevVersion, newVersion);
  }
}
