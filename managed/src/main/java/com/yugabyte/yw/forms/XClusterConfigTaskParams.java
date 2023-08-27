// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.NoArgsConstructor;
import org.yb.master.MasterDdlOuterClass;

@NoArgsConstructor
@Getter
public class XClusterConfigTaskParams extends UniverseDefinitionTaskParams {

  public XClusterConfig xClusterConfig;
  @JsonIgnore protected List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList;
  protected Set<String> sourceTableIdsWithNoTableOnTargetUniverse;
  protected Map<String, List<String>> mainTableIndexTablesMap;
  protected XClusterConfigCreateFormData.BootstrapParams bootstrapParams;
  protected XClusterConfigEditFormData editFormData;
  protected XClusterConfigSyncFormData syncFormData;
  protected Set<String> tableIdsToAdd;
  protected Set<String> tableIdsToRemove;
  protected boolean isForced = false;
  protected DrConfigCreateForm.PitrParams pitrParams;

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Map<String, String> sourceTableIdTargetTableIdMap,
      @Nullable DrConfigCreateForm.PitrParams pitrParams) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.bootstrapParams = bootstrapParams;
    this.mainTableIndexTablesMap = mainTableIndexTablesMap;
    this.sourceTableIdsWithNoTableOnTargetUniverse =
        sourceTableIdTargetTableIdMap.entrySet().stream()
            .filter(entry -> Objects.isNull(entry.getValue()))
            .map(Entry::getKey)
            .collect(Collectors.toSet());
    this.tableInfoList = tableInfoList;
    this.pitrParams = pitrParams;
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Map<String, String> sourceTableIdTargetTableIdMap) {
    this(
        xClusterConfig,
        bootstrapParams,
        tableInfoList,
        mainTableIndexTablesMap,
        sourceTableIdTargetTableIdMap,
        null /* pitrParams */);
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigEditFormData editFormData,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Set<String> tableIdsToAdd,
      Map<String, String> sourceTableIdTargetTableIdMap,
      Set<String> tableIdsToRemove) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.editFormData = editFormData;
    this.bootstrapParams = editFormData.bootstrapParams;
    this.mainTableIndexTablesMap = mainTableIndexTablesMap;
    this.tableIdsToAdd = tableIdsToAdd;
    this.sourceTableIdsWithNoTableOnTargetUniverse =
        sourceTableIdTargetTableIdMap.entrySet().stream()
            .filter(entry -> Objects.isNull(entry.getValue()))
            .map(Entry::getKey)
            .collect(Collectors.toSet());
    this.tableIdsToRemove = tableIdsToRemove;
    this.tableInfoList = tableInfoList;
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Map<String, String> sourceTableIdTargetTableIdMap,
      boolean isForced) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.bootstrapParams = bootstrapParams;
    this.mainTableIndexTablesMap = mainTableIndexTablesMap;
    this.tableInfoList = tableInfoList;
    this.tableIdsToAdd = XClusterConfigTaskBase.getTableIds(this.tableInfoList);
    this.isForced = isForced;
    this.sourceTableIdsWithNoTableOnTargetUniverse =
        sourceTableIdTargetTableIdMap.entrySet().stream()
            .filter(entry -> Objects.isNull(entry.getValue()))
            .map(Entry::getKey)
            .collect(Collectors.toSet());
    ;
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

  public XClusterConfigTaskParams(XClusterConfigSyncFormData syncFormData) {
    this.syncFormData = syncFormData;
    this.setUniverseUUID(syncFormData.targetUniverseUUID);
  }
}
