// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.NoArgsConstructor;
import org.yb.master.MasterDdlOuterClass;

@NoArgsConstructor
@Getter
public class XClusterConfigTaskParams extends UniverseDefinitionTaskParams {

  public XClusterConfig xClusterConfig;
  @JsonIgnore private List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList;
  @JsonIgnore private MasterDdlOuterClass.ListTablesResponsePB.TableInfo txnTableInfo;
  private Set<String> sourceTableIdsWithNoTableOnTargetUniverse;
  private Map<String, List<String>> mainTableIndexTablesMap;
  private XClusterConfigCreateFormData.BootstrapParams bootstrapParams;
  private XClusterConfigEditFormData editFormData;
  private XClusterConfigSyncFormData syncFormData;
  private Set<String> tableIdsToAdd;
  private Set<String> tableIdsToRemove;
  private boolean isForced = false;

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Set<String> sourceTableIdsWithNoTableOnTargetUniverse) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.bootstrapParams = bootstrapParams;
    this.mainTableIndexTablesMap = mainTableIndexTablesMap;
    this.setTableInfoListAndTxnTableInfo(tableInfoList);
    this.sourceTableIdsWithNoTableOnTargetUniverse = sourceTableIdsWithNoTableOnTargetUniverse;
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigEditFormData editFormData,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Set<String> tableIdsToAdd,
      Set<String> tableIdsToRemove) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.editFormData = editFormData;
    this.bootstrapParams = editFormData.bootstrapParams;
    this.mainTableIndexTablesMap = mainTableIndexTablesMap;
    this.tableIdsToAdd = tableIdsToAdd;
    this.tableIdsToRemove = tableIdsToRemove;
    this.setTableInfoListAndTxnTableInfo(tableInfoList);
    this.sourceTableIdsWithNoTableOnTargetUniverse = Collections.emptySet();
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Set<String> sourceTableIdsWithNoTableOnTargetUniverse,
      boolean isForced) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.bootstrapParams = bootstrapParams;
    this.mainTableIndexTablesMap = mainTableIndexTablesMap;
    this.setTableInfoListAndTxnTableInfo(tableInfoList);
    this.tableIdsToAdd = XClusterConfigTaskBase.getTableIds(this.tableInfoList);
    this.isForced = isForced;
    this.sourceTableIdsWithNoTableOnTargetUniverse = sourceTableIdsWithNoTableOnTargetUniverse;
  }

  public XClusterConfigTaskParams(XClusterConfig xClusterConfig) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
  }

  public XClusterConfigTaskParams(XClusterConfig xClusterConfig, boolean isForced) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.isForced = isForced;
  }

  public XClusterConfigTaskParams(UUID targetUniverseUUID) {
    this.setUniverseUUID(targetUniverseUUID);
  }

  public void setTableInfoListAndTxnTableInfo(
      @Nullable List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList) {
    if (Objects.nonNull(tableInfoList)) {
      this.tableInfoList = tableInfoList;
      txnTableInfo = XClusterConfigTaskBase.getTxnTableInfoIfExists(tableInfoList).orElse(null);
      if (Objects.nonNull(txnTableInfo)) {
        // Todo: Remove the dependency on the order in tableInfoList.
        this.tableInfoList.remove(this.tableInfoList.size() - 1);
      }
    }
  }

  public XClusterConfigTaskParams(XClusterConfigSyncFormData syncFormData) {
    this.syncFormData = syncFormData;
    this.setUniverseUUID(syncFormData.targetUniverseUUID);
  }
}
