// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Set;

public class XClusterUtil {
  public static final String MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_STABLE = "2024.1.3.0-b105";
  public static final String MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_PREVIEW = "2.23.0.0-b394";

  public static final String MULTIPLE_TXN_REPLICATION_SUPPORT_VERSION_STABLE = "2024.1.0.0-b71";
  public static final String MULTIPLE_TXN_REPLICATION_SUPPORT_VERSION_PREVIEW = "2.23.0.0-b157";

  public static boolean supportsDbScopedXCluster(Universe universe) {
    // The minimum YBDB version that supports db scoped replication is 2024.1.1.0-b49 stable and
    //   2.23.0.0-b394 for preview.
    String softwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (universe
        .getUniverseDetails()
        .softwareUpgradeState
        .equals(SoftwareUpgradeState.PreFinalize)) {
      if (universe.getUniverseDetails().prevYBSoftwareConfig != null) {
        softwareVersion = universe.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion();
      }
    }
    return Util.compareYBVersions(
            softwareVersion,
            MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_STABLE,
            MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_PREVIEW,
            true /* suppressFormatError */)
        >= 0;
  }

  public static boolean supportMultipleTxnReplication(Universe universe) {
    String softwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (universe
        .getUniverseDetails()
        .softwareUpgradeState
        .equals(SoftwareUpgradeState.PreFinalize)) {
      if (universe.getUniverseDetails().prevYBSoftwareConfig != null) {
        softwareVersion = universe.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion();
      }
    }
    return Util.compareYBVersions(
            softwareVersion,
            MULTIPLE_TXN_REPLICATION_SUPPORT_VERSION_STABLE,
            MULTIPLE_TXN_REPLICATION_SUPPORT_VERSION_PREVIEW,
            true /* suppressFormatError */)
        >= 0;
  }

  public static void checkDbScopedXClusterSupported(
      Universe sourceUniverse, Universe targetUniverse) {
    // Check YBDB software version.
    if (!supportsDbScopedXCluster(sourceUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Db scoped XCluster is not supported in this version of the source universe (%s);"
                  + " please upgrade to a stable version >= %s or preview version >= %s",
              sourceUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_STABLE,
              MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_PREVIEW));
    }
    if (!supportsDbScopedXCluster(targetUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Db scoped XCluster is not supported in this version of the target universe (%s);"
                  + " please upgrade to a stable version >= %s or preview version >= %s",
              targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_STABLE,
              MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_PREVIEW));
    }
  }

  public static void checkDbScopedNonEmptyDbs(Set<String> dbIds) {
    if (dbIds.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "No databases were passed in. Db scoped XCluster does not support replication with no"
              + " databases.");
    }
  }

  public static void dbScopedXClusterPreChecks(
      Universe sourceUniverse, Universe targetUniverse, Set<String> dbIds) {
    checkDbScopedXClusterSupported(sourceUniverse, targetUniverse);
    checkDbScopedNonEmptyDbs(dbIds);

    // TODO: Validate dbIds passed in exist on source universe.
    // TODO: Validate namespace names exist on both source and target universe.
  }

  public static XClusterTableConfig.Status dbStatusToTableStatus(
      XClusterNamespaceConfig.Status namespaceStatus) {
    switch (namespaceStatus) {
      case Failed:
        return XClusterTableConfig.Status.Failed;
      case Warning:
        return XClusterTableConfig.Status.Warning;
      case Updating:
        return XClusterTableConfig.Status.Updating;
      case Bootstrapping:
        return XClusterTableConfig.Status.Bootstrapping;
      case Validated:
        return XClusterTableConfig.Status.Validated;
      case Running:
        return XClusterTableConfig.Status.Running;
      default:
        return XClusterTableConfig.Status.Error;
    }
  }

  public static void ensureYsqlMajorUpgradeIsComplete(
      SoftwareUpgradeHelper softwareUpgradeHelper,
      Universe sourceUniverse,
      Universe targetUniverse) {
    if (softwareUpgradeHelper.isYsqlMajorUpgradeIncomplete(sourceUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot configure XCluster/DR config because YSQL major version upgrade on source"
              + " universe is in progress.");
    }

    if (softwareUpgradeHelper.isYsqlMajorUpgradeIncomplete(targetUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot configure XCluster/DR config because YSQL major version upgrade on target"
              + " universe is in progress.");
    }
  }
}
