package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.XClusterUtil.checkDbScopedNonEmptyDbs;
import static com.yugabyte.yw.common.XClusterUtil.checkDbScopedXClusterSupported;
import static com.yugabyte.yw.controllers.XClusterConfigController.certsForCdcDirGFlagCheck;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;

import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import jakarta.inject.Inject;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;

@Slf4j
public class XClusterCreatePrecheck {
  private final YBClientService ybService;
  private final RuntimeConfGetter confGetter;
  private final SoftwareUpgradeHelper softwareUpgradeHelper;

  @Inject
  public XClusterCreatePrecheck(
      YBClientService ybService,
      RuntimeConfGetter confGetter,
      SoftwareUpgradeHelper softwareUpgradeHelper) {
    this.ybService = ybService;
    this.confGetter = confGetter;
    this.softwareUpgradeHelper = softwareUpgradeHelper;
  }

  public void xClusterCreatePreChecks(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      XClusterConfig.ConfigType configType,
      Universe sourceUniverse,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList,
      Universe targetUniverse,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList) {

    if (requestedTableInfoList.isEmpty()) {
      throw new IllegalArgumentException("requestedTableInfoList is empty");
    }

    CommonTypes.TableType tableType = XClusterConfigTaskBase.getTableType(requestedTableInfoList);

    if (configType == XClusterConfig.ConfigType.Txn) {
      transactionalXClusterPreChecks(sourceUniverse, targetUniverse, tableType);
    } else if (configType == XClusterConfig.ConfigType.Db) {
      dbScopedXClusterPreChecks(
          sourceUniverse, targetUniverse, requestedTableInfoList, sourceTableInfoList);
    }

    Set<String> tableIds = XClusterConfigTaskBase.getTableIds(requestedTableInfoList);

    if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
      XClusterUtil.ensureUpgradeIsComplete(sourceUniverse, targetUniverse);
    }

    XClusterConfigTaskBase.verifyTablesNotInReplication(
        ybService,
        tableIds,
        TableInfoUtil.getXClusterConfigTableType(requestedTableInfoList),
        configType,
        sourceUniverse.getUniverseUUID(),
        sourceTableInfoList,
        targetUniverse.getUniverseUUID(),
        targetTableInfoList,
        false /* skipTxnReplicationCheck */);

    certsForCdcDirGFlagCheck(sourceUniverse, targetUniverse);

    // XCluster replication can be set up only for YCQL and YSQL tables.
    if (tableType != CommonTypes.TableType.YQL_TABLE_TYPE
        && tableType != CommonTypes.TableType.PGSQL_TABLE_TYPE) {
      throw new IllegalArgumentException(
          String.format(
              "XCluster replication can be set up only for YCQL and YSQL tables: "
                  + "type %s requested",
              tableType));
    }

    // There cannot exist more than one xCluster config when there is a txn xCluster config.
    List<XClusterConfig> sourceUniverseXClusterConfigs =
        XClusterConfig.getByUniverseUuid(sourceUniverse.getUniverseUUID());
    List<XClusterConfig> targetUniverseXClusterConfigs =
        XClusterConfig.getByUniverseUuid(targetUniverse.getUniverseUUID());
    if (sourceUniverseXClusterConfigs.stream()
            .anyMatch(xClusterConfig -> xClusterConfig.getType() == XClusterConfig.ConfigType.Txn)
        || targetUniverseXClusterConfigs.stream()
            .anyMatch(
                xClusterConfig -> xClusterConfig.getType() == XClusterConfig.ConfigType.Txn)) {
      if (!confGetter.getConfForScope(
              sourceUniverse, UniverseConfKeys.allowMultipleTxnReplicationConfigs)
          || !confGetter.getConfForScope(
              targetUniverse, UniverseConfKeys.allowMultipleTxnReplicationConfigs)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Multiple Txn replications are not allowed. Please set runtime config "
                + "'yb.xcluster.transactional.allow_multiple_configs' to true in"
                + "both source and target universes.");
      }
      if (!XClusterUtil.supportMultipleTxnReplication(sourceUniverse)
          || !XClusterUtil.supportMultipleTxnReplication(targetUniverse)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "At least one of the universes has a txn xCluster config. There cannot exist any other"
                + " xCluster config when there is a txn xCluster config on universes below version"
                + " 2024.1.0.0-b71/2.23.0.0-b157.");
      }
    }

    // Make sure only supported relations types are passed in by the user.
    Map<Boolean, List<String>> tableIdsPartitionedByIsXClusterSupported =
        XClusterConfigTaskBase.getTableIdsPartitionedByIsXClusterSupported(requestedTableInfoList);
    if (!tableIdsPartitionedByIsXClusterSupported.get(false).isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Only the following relation types are supported for xCluster replication: %s; The"
                  + " following tables have different relation types or is a colocated child table:"
                  + " %s",
              XClusterConfigTaskBase.X_CLUSTER_SUPPORTED_TABLE_RELATION_TYPE_SET,
              tableIdsPartitionedByIsXClusterSupported.get(false)));
    }

    // TODO: Validate colocated child tables have the same colocation id.
  }

  public void transactionalXClusterPreChecks(
      Universe sourceUniverse, Universe targetUniverse, CommonTypes.TableType tableType) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.transactionalXClusterEnabled)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Support for transactional xCluster configs is disabled in YBA. You may enable it "
              + "by setting yb.xcluster.transactional.enabled to true in the application.conf");
    }

    // Check YBDB software version.
    if (!XClusterConfigTaskBase.supportsTxnXCluster(sourceUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Transactional XCluster is not supported in this version of the "
                  + "source universe (%s); please upgrade to a version >= %s",
              sourceUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              XClusterConfigTaskBase.MINIMUN_VERSION_TRANSACTIONAL_XCLUSTER_SUPPORT));
    }
    if (!XClusterConfigTaskBase.supportsTxnXCluster(targetUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Transactional XCluster is not supported in this version of the "
                  + "target universe (%s); please upgrade to a version >= %s",
              targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              XClusterConfigTaskBase.MINIMUN_VERSION_TRANSACTIONAL_XCLUSTER_SUPPORT));
    }

    // There cannot exist more than one xCluster config when its type is transactional.
    List<XClusterConfig> sourceUniverseXClusterConfigs =
        XClusterConfig.getByUniverseUuid(sourceUniverse.getUniverseUUID());
    List<XClusterConfig> targetUniverseXClusterConfigs =
        XClusterConfig.getByUniverseUuid(targetUniverse.getUniverseUUID());

    if (!sourceUniverseXClusterConfigs.isEmpty() || !targetUniverseXClusterConfigs.isEmpty()) {
      if (!confGetter.getConfForScope(
              sourceUniverse, UniverseConfKeys.allowMultipleTxnReplicationConfigs)
          || !confGetter.getConfForScope(
              targetUniverse, UniverseConfKeys.allowMultipleTxnReplicationConfigs)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Multiple Txn replications are not allowed. Please set runtime config "
                + "'yb.xcluster.transactional.allow_multiple_configs' to true in"
                + "both source and target universes.");
      }
      if (!XClusterUtil.supportMultipleTxnReplication(sourceUniverse)
          || !XClusterUtil.supportMultipleTxnReplication(targetUniverse)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "To create a transactional xCluster, you have to delete all the existing xCluster"
                + " configs on the source and target universes. There could exist at most one"
                + " transactional xCluster config on universe below versions"
                + " 2024.1.0.0-b71/2.23.0.0-b157.");
      }
    }

    // Txn xCluster is supported only for YSQL tables.
    if (!tableType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)) {
      throw new IllegalArgumentException(
          String.format(
              "Transaction xCluster is supported only for YSQL tables. Table type %s is selected",
              tableType));
    }
  }

  public void dbScopedXClusterPreChecks(
      Universe sourceUniverse,
      Universe targetUniverse,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList) {
    checkDbScopedXClusterSupported(sourceUniverse, targetUniverse);
    Set<String> requestedDbIds =
        requestedTableInfoList.stream()
            .map(ti -> ti.getNamespace().getId().toStringUtf8())
            .collect(Collectors.toSet());
    checkDbScopedNonEmptyDbs(requestedDbIds);
    Set<String> nonExistentSourceDBs =
        Sets.difference(
            requestedDbIds,
            sourceTableInfoList.stream()
                .map(ti -> ti.getNamespace().getId().toStringUtf8())
                .collect(Collectors.toSet()));

    if (!nonExistentSourceDBs.isEmpty()) {
      throw new PlatformServiceException(
          FORBIDDEN,
          String.format("Namespaces %s don't exist on the source universe", nonExistentSourceDBs));
    }

    // TODO: Validate namespace names exist on both source and target universe.
  }
}
