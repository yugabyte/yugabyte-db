// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import org.yb.master.MasterDdlOuterClass;

@NoArgsConstructor
@Getter
public class XClusterConfigTaskParams extends UniverseDefinitionTaskParams {

  public XClusterConfig xClusterConfig;
  @JsonIgnore private List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList;
  private Map<String, List<String>> mainTableIndexTablesMap;
  private XClusterConfigCreateFormData.BootstrapParams bootstrapParams;
  private XClusterConfigEditFormData editFormData;
  private Set<String> tableIdsToRemove;
  private boolean isForced = false;

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
    this.bootstrapParams = bootstrapParams;
    this.tableInfoList = tableInfoList;
    this.mainTableIndexTablesMap = mainTableIndexTablesMap;
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigEditFormData editFormData,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Set<String> tableIdsToRemove) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
    this.editFormData = editFormData;
    this.bootstrapParams = editFormData.bootstrapParams;
    this.tableInfoList = tableInfoList;
    this.mainTableIndexTablesMap = mainTableIndexTablesMap;
    this.tableIdsToRemove = tableIdsToRemove;
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigRestartFormData restartFormData,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      boolean isForced) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
    this.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    this.bootstrapParams.backupRequestParams = restartFormData.bootstrapParams.backupRequestParams;
    this.tableInfoList = tableInfoList;
    this.mainTableIndexTablesMap = mainTableIndexTablesMap;
    this.isForced = isForced;
  }

  public XClusterConfigTaskParams(XClusterConfig xClusterConfig) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
  }

  public XClusterConfigTaskParams(XClusterConfig xClusterConfig, boolean isForced) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
    this.isForced = isForced;
  }

  public XClusterConfigTaskParams(UUID targetUniverseUUID) {
    this.universeUUID = targetUniverseUUID;
  }
}
