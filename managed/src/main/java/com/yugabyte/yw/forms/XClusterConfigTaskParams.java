// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonGetter;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonSetter;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.ArrayList;
import java.util.Base64;
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
import lombok.Setter;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;

@NoArgsConstructor
@Getter
@JsonDeserialize(converter = XClusterConfigTaskParams.Converter.class)
public class XClusterConfigTaskParams extends UniverseDefinitionTaskParams {

  public XClusterConfig xClusterConfig;
  @JsonIgnore protected List<TableInfo> tableInfoList;
  protected Set<String> sourceTableIdsWithNoTableOnTargetUniverse;
  protected Map<String, List<String>> mainTableIndexTablesMap;
  @Setter protected XClusterConfigCreateFormData.BootstrapParams bootstrapParams;
  protected XClusterConfigEditFormData editFormData;
  protected XClusterConfigSyncFormData syncFormData;
  protected Set<String> tableIdsToAdd;
  protected Set<String> tableIdsToRemove;
  protected boolean isForced = false;
  @Setter protected DrConfigCreateForm.PitrParams pitrParams;
  protected boolean isForceBootstrap = false;
  public Set<String> dbs;
  protected Set<String> databaseIdsToAdd;
  protected Set<String> databaseIdsToRemove;

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<TableInfo> tableInfoList,
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
    if (this.pitrParams == null) {
      updatePitrParams(xClusterConfig);
    }
  }

  /** It is used in the basic/txn create method. */
  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Map<String, String> sourceTableIdTargetTableIdMap) {
    this(
        xClusterConfig,
        bootstrapParams,
        tableInfoList,
        mainTableIndexTablesMap,
        sourceTableIdTargetTableIdMap,
        null /* pitrParams */);
    if (this.pitrParams == null) {
      updatePitrParams(xClusterConfig);
    }
  }

  /** It is used in the db scoped create method. */
  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      Set<String> dbs,
      @Nullable DrConfigCreateForm.PitrParams pitrParams) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.bootstrapParams = bootstrapParams;
    this.dbs = dbs;
    this.pitrParams = pitrParams;
    if (this.pitrParams == null) {
      updatePitrParams(xClusterConfig);
    }
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      Set<String> dbs,
      @Nullable DrConfigCreateForm.PitrParams pitrParams,
      boolean isForceBootstrap) {
    this(xClusterConfig, bootstrapParams, dbs, pitrParams);
    this.isForceBootstrap = isForceBootstrap;
    if (this.pitrParams == null) {
      updatePitrParams(xClusterConfig);
    }
  }

  /** It is used in the edit method. */
  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigEditFormData editFormData,
      List<TableInfo> tableInfoList,
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
    updatePitrParams(xClusterConfig);

    // Todo: add support for accepting pitr params from the user for the transactional xCluster
    //  to make it consistent with the DR config api.
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      XClusterConfigEditFormData editFormData,
      Set<String> databaseIdsToAdd,
      Set<String> databaseIdsToRemove) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.editFormData = editFormData;
    this.databaseIdsToAdd = databaseIdsToAdd;
    this.databaseIdsToRemove = databaseIdsToRemove;
    updatePitrParams(xClusterConfig);
  }

  /** It is used in the restart method. */
  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Map<String, String> sourceTableIdTargetTableIdMap,
      boolean isForced,
      boolean isForceBootstrap) {
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
    this.isForceBootstrap = isForceBootstrap;
    updatePitrParams(xClusterConfig);

    // Todo: add support for accepting pitr params from the user for the transactional xCluster
    //  to make it consistent with the DR config api.
  }

  public XClusterConfigTaskParams(XClusterConfig xClusterConfig) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
  }

  /** It is used in the delete method. */
  public XClusterConfigTaskParams(XClusterConfig xClusterConfig, boolean isForced) {
    this.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    this.xClusterConfig = xClusterConfig;
    this.isForced = isForced;
  }

  /** It is used in the sync method. */
  public XClusterConfigTaskParams(UUID targetUniverseUUID) {
    this.setUniverseUUID(targetUniverseUUID);
  }

  /** It is used in the sync method. */
  public XClusterConfigTaskParams(XClusterConfigSyncFormData syncFormData) {
    this.syncFormData = syncFormData;
    this.setUniverseUUID(syncFormData.targetUniverseUUID);
  }

  public void updatePitrParams(XClusterConfig xClusterConfig) {
    if (xClusterConfig.getType().equals(XClusterConfig.ConfigType.Basic)) {
      return;
    }

    RuntimeConfGetter confGetter =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);

    // Fetch the previous default values for pitr params from the target universe runtime config
    // as old DR configs might not have these parameters.
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    long pitrRetentionPeriodSec =
        confGetter
            .getConfForScope(targetUniverse, UniverseConfKeys.txnXClusterPitrDefaultRetentionPeriod)
            .getSeconds();
    ;
    long pitrSnapshotIntervalSec =
        confGetter
            .getConfForScope(
                targetUniverse, UniverseConfKeys.txnXClusterPitrDefaultSnapshotInterval)
            .getSeconds();
    ;
    DrConfig drConfig = xClusterConfig.getDrConfig();
    if (drConfig != null
        && drConfig.getPitrRetentionPeriodSec() != null
        && drConfig.getPitrSnapshotIntervalSec() != null) {
      if (drConfig.getPitrRetentionPeriodSec() != null) {
        pitrSnapshotIntervalSec = drConfig.getPitrSnapshotIntervalSec();
      }
      if (drConfig.getPitrSnapshotIntervalSec() != null) {
        pitrRetentionPeriodSec = drConfig.getPitrRetentionPeriodSec();
      }
    }
    this.pitrParams =
        new DrConfigCreateForm.PitrParams(pitrRetentionPeriodSec, pitrSnapshotIntervalSec);
  }

  /** It is used in the edit DR Config method. */
  public XClusterConfigTaskParams(DrConfigEditForm drConfigEditForm) {
    this.pitrParams = drConfigEditForm.pitrParams;
    if (editFormData.bootstrapParams != null) {
      editFormData.bootstrapParams.backupRequestParams =
          editFormData.bootstrapParams.backupRequestParams;
    }
  }

  public void refreshIfExists() {
    // The entities that have non-serializable fields should be refreshed after deserialization that
    //  happens during task retry.
    if (Objects.nonNull(xClusterConfig)) {
      xClusterConfig = XClusterConfig.maybeGet(xClusterConfig.getUuid()).orElse(null);
    }
  }

  @JsonGetter
  private List<String> getTableInfoAsBytesList() {
    if (tableInfoList == null) {
      return null;
    }
    List<String> tableInfoAsBytesList = new ArrayList<>();
    tableInfoList.forEach(
        tableInfo -> {
          ByteString byteString = ByteString.copyFrom(tableInfo.toByteArray());
          tableInfoAsBytesList.add(Base64.getEncoder().encodeToString(byteString.toByteArray()));
        });
    return tableInfoAsBytesList;
  }

  @JsonSetter
  private void setTableInfoAsBytesList(List<String> tableInfoAsBytesList) {
    if (tableInfoAsBytesList == null) {
      tableInfoList = null;
      return;
    }
    tableInfoList = new ArrayList<>();
    tableInfoAsBytesList.forEach(
        encodedString -> {
          try {
            byte[] bytes = Base64.getDecoder().decode(encodedString);
            tableInfoList.add(TableInfo.parseFrom(bytes));
          } catch (Exception e) {
            throw new RuntimeException(e);
          }
        });
  }

  public static class Converter extends BaseConverter<XClusterConfigTaskParams> {}
}
