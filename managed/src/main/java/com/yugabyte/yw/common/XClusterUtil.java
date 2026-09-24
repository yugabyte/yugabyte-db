// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import org.yb.master.MasterDdlOuterClass;

public class XClusterUtil {
  public static final String MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_STABLE = "2024.1.3.0-b105";
  public static final String MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_PREVIEW = "2.23.0.0-b394";

  public static final String MULTIPLE_TXN_REPLICATION_SUPPORT_VERSION_STABLE = "2024.1.0.0-b71";
  public static final String MULTIPLE_TXN_REPLICATION_SUPPORT_VERSION_PREVIEW = "2.23.0.0-b157";

  public static final String MINIMUM_VERSION_AUTOMATIC_DDL_SUPPORT_STABLE = "2025.2.1.0-b0";
  public static final String MINIMUM_VERSION_AUTOMATIC_DDL_SUPPORT_PREVIEW = "2.29.0.0-b0";

  // YBDB replicates materialized views only in automatic DDL mode, and only starting from these
  // versions.
  public static final String MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE = "2025.2.1.0-b124";
  public static final String MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW = "2.29.0.0-b400";

  /**
   * Returns the YBDB version of the universe that xCluster feature gates must be evaluated against.
   * While a software upgrade is not finalized yet, the universe still behaves like the version it
   * is being upgraded from, so that version is returned instead.
   */
  private static String getSoftwareVersionForFeatureGating(Universe universe) {
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
    return softwareVersion;
  }

  public static boolean supportsDbScopedXCluster(Universe universe) {
    // The minimum YBDB version that supports db scoped replication is 2024.1.1.0-b49 stable and
    //   2.23.0.0-b394 for preview.
    return Util.compareYBVersions(
            getSoftwareVersionForFeatureGating(universe),
            MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_STABLE,
            MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_PREVIEW,
            true /* suppressFormatError */)
        >= 0;
  }

  public static boolean supportMultipleTxnReplication(Universe universe) {
    return Util.compareYBVersions(
            getSoftwareVersionForFeatureGating(universe),
            MULTIPLE_TXN_REPLICATION_SUPPORT_VERSION_STABLE,
            MULTIPLE_TXN_REPLICATION_SUPPORT_VERSION_PREVIEW,
            true /* suppressFormatError */)
        >= 0;
  }

  public static boolean supportsAutomaticDdl(Universe universe) {
    return Util.compareYBVersions(
            getSoftwareVersionForFeatureGating(universe),
            MINIMUM_VERSION_AUTOMATIC_DDL_SUPPORT_STABLE,
            MINIMUM_VERSION_AUTOMATIC_DDL_SUPPORT_PREVIEW,
            true /* suppressFormatError */)
        > 0;
  }

  /**
   * Returns whether the YBDB version of the universe can replicate materialized views through
   * xCluster.
   */
  public static boolean supportsMatviewXCluster(Universe universe) {
    return Util.compareYBVersions(
            getSoftwareVersionForFeatureGating(universe),
            MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE,
            MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW,
            true /* suppressFormatError */)
        >= 0;
  }

  /**
   * Materialized views can take part in replication only in automatic DDL mode, and only when both
   * the source and the target universes run a YBDB version that replicates them. Semi-automatic (db
   * scoped without automatic DDL) and manual (basic/txn) configs never replicate materialized
   * views, regardless of the YBDB version.
   *
   * @param automaticDdlMode whether the replication config is in automatic DDL mode
   * @param sourceUniverse the source universe of the replication config
   * @param targetUniverse the target universe of the replication config
   * @return true if materialized views may be part of the replication config
   */
  public static boolean isMatviewReplicationSupported(
      boolean automaticDdlMode, Universe sourceUniverse, Universe targetUniverse) {
    return automaticDdlMode
        && supportsMatviewXCluster(sourceUniverse)
        && supportsMatviewXCluster(targetUniverse);
  }

  /**
   * Same as {@link #isMatviewReplicationSupported(boolean, Universe, Universe)}, deriving the mode
   * and the participating universes from an existing config. Returns false if either universe no
   * longer exists.
   */
  public static boolean isMatviewReplicationSupported(XClusterConfig xClusterConfig) {
    if (!Boolean.TRUE.equals(xClusterConfig.isAutomaticDdlMode())) {
      return false;
    }
    Optional<Universe> sourceUniverse =
        Objects.isNull(xClusterConfig.getSourceUniverseUUID())
            ? Optional.empty()
            : Universe.maybeGet(xClusterConfig.getSourceUniverseUUID());
    Optional<Universe> targetUniverse =
        Objects.isNull(xClusterConfig.getTargetUniverseUUID())
            ? Optional.empty()
            : Universe.maybeGet(xClusterConfig.getTargetUniverseUUID());
    return sourceUniverse.isPresent()
        && targetUniverse.isPresent()
        && isMatviewReplicationSupported(
            true /* automaticDdlMode */, sourceUniverse.get(), targetUniverse.get());
  }

  /**
   * Rejects a request that explicitly asks for materialized views to be part of a replication
   * config that cannot replicate them. The error distinguishes the two reasons: the config is not
   * in automatic DDL mode (a semantic restriction), or it is but at least one of the universes runs
   * a YBDB version that does not replicate materialized views yet.
   *
   * @param requestedTableInfoList the tables requested to be in replication
   * @param automaticDdlMode whether the replication config is in automatic DDL mode
   * @param sourceUniverse the source universe of the replication config
   * @param targetUniverse the target universe of the replication config
   */
  public static void checkMatviewReplicationSupported(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      boolean automaticDdlMode,
      Universe sourceUniverse,
      Universe targetUniverse) {
    Set<String> matviewTableIds =
        requestedTableInfoList.stream()
            .filter(TableInfoUtil::isMatviewTable)
            .map(tableInfo -> tableInfo.getId().toStringUtf8())
            .collect(Collectors.toSet());
    if (matviewTableIds.isEmpty()) {
      return;
    }
    if (!automaticDdlMode) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Materialized views can be part of replication only for xCluster/DR configs in"
                  + " automatic DDL mode; the following requested tables are materialized views:"
                  + " %s",
              matviewTableIds));
    }
    if (!supportsMatviewXCluster(sourceUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Replicating materialized views is not supported in this version of the source"
                  + " universe (%s); please upgrade to a stable version >= %s or preview version >="
                  + " %s; the following requested tables are materialized views: %s",
              getSoftwareVersionForFeatureGating(sourceUniverse),
              MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE,
              MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW,
              matviewTableIds));
    }
    if (!supportsMatviewXCluster(targetUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Replicating materialized views is not supported in this version of the target"
                  + " universe (%s); please upgrade to a stable version >= %s or preview version >="
                  + " %s; the following requested tables are materialized views: %s",
              getSoftwareVersionForFeatureGating(targetUniverse),
              MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE,
              MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW,
              matviewTableIds));
    }
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
