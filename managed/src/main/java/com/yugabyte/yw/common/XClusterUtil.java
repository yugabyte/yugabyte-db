// Copyright (c) YugabyteDB, Inc.

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

  public static final String MINIMUM_VERSION_AUTOMATIC_DDL_SUPPORT_STABLE = "2025.2.1.0-b0";
  public static final String MINIMUM_VERSION_AUTOMATIC_DDL_SUPPORT_PREVIEW = "2.29.0.0-b0";

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

  public static boolean supportsAutomaticDdl(Universe universe) {
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
            MINIMUM_VERSION_AUTOMATIC_DDL_SUPPORT_STABLE,
            MINIMUM_VERSION_AUTOMATIC_DDL_SUPPORT_PREVIEW,
            true /* suppressFormatError */)
        > 0;
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

  public static XClusterTableConfig.Status dbStatusToTableStatus(
      XClusterNamespaceConfig.Status namespaceStatus) {
    return switch (namespaceStatus) {
      case Failed -> XClusterTableConfig.Status.Failed;
      case Warning -> XClusterTableConfig.Status.Warning;
      case Updating -> XClusterTableConfig.Status.Updating;
      case Bootstrapping -> XClusterTableConfig.Status.Bootstrapping;
      case Validated -> XClusterTableConfig.Status.Validated;
      case Running -> XClusterTableConfig.Status.Running;
      default -> XClusterTableConfig.Status.Error;
    };
  }

  public static void ensureUpgradeIsComplete(Universe sourceUniverse, Universe targetUniverse) {
    if (!sourceUniverse
        .getUniverseDetails()
        .softwareUpgradeState
        .equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot configure XCluster/DR config because source universe is not in ready software"
              + " upgrade state.");
    }
    if (!targetUniverse
        .getUniverseDetails()
        .softwareUpgradeState
        .equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot configure XCluster/DR config because target universe is not in ready software"
              + " upgrade state.");
    }
  }
}
