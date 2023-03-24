// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.BiMap;
import com.google.common.collect.ImmutableBiMap;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import io.ebean.Ebean;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.SqlUpdate;
import io.ebean.annotation.DbEnumValue;
import io.ebean.annotation.Transactional;
import io.ebean.annotation.Where;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collection;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import javax.persistence.CascadeType;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.JoinColumn;
import javax.persistence.JoinColumns;
import javax.persistence.ManyToOne;
import javax.persistence.OneToMany;
import javax.persistence.OneToOne;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;

@Slf4j
@Entity
@ApiModel(description = "xcluster config object")
public class XClusterConfig extends Model {

  public static final BiMap<TableType, CommonTypes.TableType>
      XClusterConfigTableTypeCommonTypesTableTypeBiMap =
          ImmutableBiMap.of(
              TableType.YSQL,
              CommonTypes.TableType.PGSQL_TABLE_TYPE,
              TableType.YCQL,
              CommonTypes.TableType.YQL_TABLE_TYPE);

  private static final Finder<UUID, XClusterConfig> find =
      new Finder<UUID, XClusterConfig>(XClusterConfig.class) {};

  @Id
  @ApiModelProperty(value = "XCluster config UUID")
  public UUID uuid;

  @Column(name = "config_name")
  @ApiModelProperty(value = "XCluster config name")
  public String name;

  @ManyToOne
  @JoinColumn(name = "source_universe_uuid", referencedColumnName = "universe_uuid")
  @ApiModelProperty(value = "Source Universe UUID")
  public UUID sourceUniverseUUID;

  @ManyToOne
  @JoinColumn(name = "target_universe_uuid", referencedColumnName = "universe_uuid")
  @ApiModelProperty(value = "Target Universe UUID")
  public UUID targetUniverseUUID;

  @ApiModelProperty(
      value = "Status",
      allowableValues = "Initialized, Running, Updating, DeletedUniverse, DeletionFailed, Failed")
  public XClusterConfigStatusType status;

  public enum XClusterConfigStatusType {
    Initialized("Initialized"),
    Running("Running"),
    Updating("Updating"),
    DeletedUniverse("DeletedUniverse"),
    DeletionFailed("DeletionFailed"),
    Failed("Failed");

    private final String status;

    XClusterConfigStatusType(String status) {
      this.status = status;
    }

    @Override
    @DbEnumValue
    public String toString() {
      return this.status;
    }
  }

  public enum TableType {
    UNKNOWN,
    YSQL,
    YCQL;

    @Override
    @DbEnumValue
    public String toString() {
      return super.toString();
    }
  }

  @ApiModelProperty(value = "tableType", allowableValues = "UNKNOWN, YSQL, YCQL")
  public TableType tableType;

  @ApiModelProperty(value = "Whether this xCluster replication config is paused")
  public boolean paused;

  @ApiModelProperty(value = "Create time of the xCluster config", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date createTime;

  @ApiModelProperty(
      value = "Last modify time of the xCluster config",
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date modifyTime;

  @OneToMany(
      mappedBy = "config",
      cascade = {CascadeType.PERSIST, CascadeType.REFRESH})
  @Where(clause = "(t0.txn_table_id IS NULL OR t0.txn_table_id <> t1.table_id)")
  @ApiModelProperty(value = "Tables participating in this xCluster config")
  @JsonIgnore
  public Set<XClusterTableConfig> tables;

  @OneToOne(cascade = CascadeType.ALL, orphanRemoval = true)
  @JoinColumns({
    @JoinColumn(
        name = "uuid",
        referencedColumnName = "config_uuid",
        insertable = false,
        updatable = false,
        table = "xcluster_table_config"),
    @JoinColumn(
        name = "txn_table_id",
        referencedColumnName = "table_id",
        table = "xcluster_config"),
  })
  @ApiModelProperty(value = "The transaction status table config")
  @JsonIgnore
  public XClusterTableConfig txnTableConfig;

  @ApiModelProperty(value = "Replication group name in DB")
  private String replicationGroupName;

  public enum ConfigType {
    Basic,
    Txn;

    @Override
    @DbEnumValue
    public String toString() {
      return super.toString();
    }

    public static ConfigType getFromString(@Nullable String value) {
      if (Objects.isNull(value)) {
        return ConfigType.Basic; // Default value
      }
      return Enum.valueOf(ConfigType.class, value);
    }
  }

  @ApiModelProperty(value = "Whether the config is txn xCluster")
  public ConfigType type;

  @ApiModelProperty(value = "Whether the source is active in txn xCluster")
  public boolean sourceActive;

  @ApiModelProperty(value = "Whether the source is active in txn xCluster")
  public boolean targetActive;

  @ApiModelProperty(value = "Replication group name that replicates the transaction status table")
  public String txnTableReplicationGroupName;

  @Override
  public String toString() {
    if (Objects.nonNull(this.txnTableConfig)) {
      return this.getReplicationGroupName()
          + "(uuid="
          + this.uuid
          + ",targetUuid="
          + this.targetUniverseUUID
          + ",status="
          + this.status
          + ",paused="
          + this.paused
          + ",tableType="
          + this.tableType
          + ",txnTable="
          + this.txnTableConfig
          + ")";
    }
    return this.getReplicationGroupName()
        + "(uuid="
        + this.uuid
        + ",targetUuid="
        + this.targetUniverseUUID
        + ",status="
        + this.status
        + ",paused="
        + this.paused
        + ",tableType="
        + this.tableType
        + ")";
  }

  public Optional<XClusterTableConfig> maybeGetTableById(String tableId) {
    // There will be at most one tableConfig for a tableId within each xCluster config.
    return this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(tableConfig -> tableConfig.tableId.equals(tableId))
        .findAny();
  }

  @JsonIgnore
  public CommonTypes.TableType getTableTypeAsCommonType() {
    if (tableType.equals(TableType.UNKNOWN)) {
      throw new RuntimeException(
          "Table type is UNKNOWN, and cannot be mapped to CommonTypes.TableType");
    }
    return XClusterConfigTableTypeCommonTypesTableTypeBiMap.get(tableType);
  }

  @JsonIgnore
  public CommonTypes.TableType setTableType(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList) {
    if (!this.tableType.equals(TableType.UNKNOWN)) {
      log.info("tableType for {} is already set; skip setting it", this);
      return getTableTypeAsCommonType();
    }
    if (tableInfoList.isEmpty()) {
      log.warn(
          "tableType for {} is unknown and cannot be deducted from tableInfoList because "
              + "it is empty",
          this);
      return getTableTypeAsCommonType();
    }
    CommonTypes.TableType typeAsCommonType = tableInfoList.get(0).getTableType();
    if (!XClusterConfigTableTypeCommonTypesTableTypeBiMap.containsValue(typeAsCommonType)) {
      throw new IllegalArgumentException(
          String.format(
              "Only %s supported as CommonTypes.TableType for xCluster replication; got %s",
              XClusterConfigTableTypeCommonTypesTableTypeBiMap.values(), typeAsCommonType));
    }
    this.tableType =
        XClusterConfigTableTypeCommonTypesTableTypeBiMap.inverse().get(typeAsCommonType);
    update();
    return typeAsCommonType;
  }

  @JsonIgnore
  public void setTxnTableId(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList) {
    Optional<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> txnTableInfoOptional =
        XClusterConfigTaskBase.getTxnTableInfoIfExists(tableInfoList);
    if (!txnTableInfoOptional.isPresent()) {
      throw new IllegalStateException(String.format("TxnTableId for %s could not be found", this));
    }
    if (this.txnTableConfig != null
        && XClusterConfigTaskBase.getTableId(txnTableInfoOptional.get())
            .equals(this.txnTableConfig.tableId)) {
      log.info("txnTable with the same id already exists");
      return;
    }
    this.txnTableConfig =
        new XClusterTableConfig(
            this, XClusterConfigTaskBase.getTableId(txnTableInfoOptional.get()));
    update();
    log.info(
        "txnTable id for xCluster config {} is set to {}", this.uuid, this.txnTableConfig.tableId);
  }

  public XClusterTableConfig getTableById(String tableId) {
    Optional<XClusterTableConfig> tableConfig = maybeGetTableById(tableId);
    if (!tableConfig.isPresent()) {
      throw new IllegalArgumentException(
          String.format(
              "Table with id (%s) does not belong to the xClusterConfig %s", tableId, this));
    }
    return tableConfig.get();
  }

  public Set<XClusterTableConfig> getTablesById(Set<String> tableIds) {
    Map<String, XClusterTableConfig> tableConfigMap =
        this.getTableDetails(true /* includeTxnTableIfExists */)
            .stream()
            .collect(
                Collectors.toMap(tableConfig -> tableConfig.tableId, tableConfig -> tableConfig));
    Set<XClusterTableConfig> tableConfigs = new HashSet<>();
    tableIds.forEach(
        tableId -> {
          XClusterTableConfig tableConfig = tableConfigMap.get(tableId);
          if (tableConfig == null) {
            throw new IllegalArgumentException(
                String.format(
                    "Table with id (%s) does not belong to the xClusterConfig %s", tableId, this));
          }
          tableConfigs.add(tableConfig);
        });
    return tableConfigs;
  }

  @JsonIgnore
  public Set<XClusterTableConfig> getTableDetails(boolean includeTxnTableIfExists) {
    if (includeTxnTableIfExists) {
      return Stream.concat(
              this.tables.stream(),
              Objects.nonNull(this.getTxnTableDetails())
                  ? Stream.of(this.getTxnTableDetails())
                  : Stream.empty())
          .collect(Collectors.toSet());
    }
    return this.tables;
  }

  @JsonProperty
  public Set<XClusterTableConfig> getTableDetails() {
    return getTableDetails(false /* includeTxnTableIfExists */);
  }

  @JsonProperty
  public XClusterTableConfig getTxnTableDetails() {
    return this.txnTableConfig;
  }

  /** @deprecated Use {@link #getTableIds} instead. */
  @Deprecated
  @JsonProperty
  public Set<String> getTables() {
    return this.tables.stream().map(table -> table.tableId).collect(Collectors.toSet());
  }

  @JsonIgnore
  public Set<String> getTableIds(boolean includeTxnTableIfExists) {
    if (includeTxnTableIfExists) {
      return this.getTableDetails(true /* includeTxnTableIfExists */)
          .stream()
          .map(table -> table.tableId)
          .collect(Collectors.toSet());
    }
    return this.tables.stream().map(table -> table.tableId).collect(Collectors.toSet());
  }

  public Set<String> getTableIds() {
    return getTableIds(false /* includeTxnTableIfExists */);
  }

  @JsonIgnore
  public Set<String> getTableIdsWithReplicationSetup(Set<String> tableIds, boolean done) {
    return this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(table -> tableIds.contains(table.tableId) && table.replicationSetupDone == done)
        .map(table -> table.tableId)
        .collect(Collectors.toSet());
  }

  @JsonIgnore
  public Set<String> getTableIdsWithReplicationSetup(boolean done) {
    return getTableIdsWithReplicationSetup(getTables(), done);
  }

  @JsonIgnore
  public Set<String> getTableIdsWithReplicationSetup() {
    return getTableIdsWithReplicationSetup(true /* done */);
  }

  @JsonIgnore
  public Set<String> getTableIds(boolean includeMainTables, boolean includeIndexTables) {
    if (!includeMainTables && !includeIndexTables) {
      throw new IllegalArgumentException(
          "Both includeMainTables and includeIndexTables cannot be false");
    }
    if (includeMainTables && includeIndexTables) {
      return this.getTables();
    }
    return this.tables
        .stream()
        .filter(tableConfig -> tableConfig.indexTable == includeIndexTables)
        .map(table -> table.tableId)
        .collect(Collectors.toSet());
  }

  @JsonIgnore
  public Set<String> getTableIdsExcludeIndexTables() {
    return getTableIds(true /* includeMainTables */, false /* includeIndexTables */);
  }

  public void setTables(Set<String> tableIds) {
    setTables(tableIds, null /* tableIdsNeedBootstrap */);
  }

  @Transactional
  public void setTables(Set<String> tableIds, Set<String> tableIdsNeedBootstrap) {
    this.tables = new HashSet<>();
    addTables(tableIds, tableIdsNeedBootstrap);
  }

  @Transactional
  public void addTables(Set<String> tableIds, Set<String> tableIdsNeedBootstrap) {
    if (tableIds == null) {
      throw new IllegalArgumentException("tableIds cannot be null");
    }
    // Ensure tableIdsNeedBootstrap is a subset of tableIds.
    if (tableIdsNeedBootstrap != null && !tableIds.containsAll(tableIdsNeedBootstrap)) {
      String errMsg =
          String.format(
              "The set of tables in tableIdsNeedBootstrap (%s) is not a subset of tableIds (%s)",
              tableIdsNeedBootstrap, tableIds);
      throw new IllegalArgumentException(errMsg);
    }
    if (this.tables == null) {
      this.tables = new HashSet<>();
    }
    tableIds.forEach(
        tableId -> {
          XClusterTableConfig tableConfig = new XClusterTableConfig(this, tableId);
          if (tableIdsNeedBootstrap != null && tableIdsNeedBootstrap.contains(tableId)) {
            tableConfig.needBootstrap = true;
          }
          addTableConfig(tableConfig);
        });
    update();
  }

  @Transactional
  public void addTablesIfNotExist(
      Set<String> tableIds, Set<String> tableIdsNeedBootstrap, boolean areIndexTables) {
    if (tableIds.isEmpty()) {
      return;
    }
    Set<String> nonExistingTableIds =
        tableIds
            .stream()
            .filter(
                tableId -> !this.getTableIds(true /* includeTxnTableIfExists */).contains(tableId))
            .collect(Collectors.toSet());
    Set<String> nonExistingTableIdsNeedBootstrap = null;
    if (tableIdsNeedBootstrap != null) {
      nonExistingTableIdsNeedBootstrap =
          tableIdsNeedBootstrap
              .stream()
              .filter(nonExistingTableIds::contains)
              .collect(Collectors.toSet());
    }
    addTables(nonExistingTableIds, nonExistingTableIdsNeedBootstrap);
    if (areIndexTables) {
      this.setIndexTableForTables(tableIds, true /* indexTable */);
    }
  }

  public void addTablesIfNotExist(Set<String> tableIds, Set<String> tableIdsNeedBootstrap) {
    addTablesIfNotExist(tableIds, tableIdsNeedBootstrap, false /* areIndexTables */);
  }

  public void addTablesIfNotExist(
      Set<String> tableIds, XClusterConfigCreateFormData.BootstrapParams bootstrapParams) {
    addTablesIfNotExist(tableIds, bootstrapParams != null ? bootstrapParams.tables : null);
  }

  @Transactional
  public void addTablesIfNotExist(Set<String> tableIds) {
    addTablesIfNotExist(tableIds, (Set<String>) null /* tableIdsNeedBootstrap */);
  }

  @Transactional
  public void addTables(Set<String> tableIds) {
    addTables(tableIds, null /* tableIdsNeedBootstrap */);
  }

  @Transactional
  public void addTables(Map<String, String> tableIdsStreamIdsMap) {
    if (this.tables == null) {
      this.tables = new HashSet<>();
    }
    tableIdsStreamIdsMap.forEach(
        (tableId, streamId) -> {
          XClusterTableConfig tableConfig = new XClusterTableConfig(this, tableId);
          tableConfig.streamId = streamId;
          addTableConfig(tableConfig);
        });
    update();
  }

  @JsonIgnore
  public Set<String> getStreamIdsWithReplicationSetup() {
    return this.tables
        .stream()
        .filter(table -> table.replicationSetupDone)
        .map(table -> table.streamId)
        .collect(Collectors.toSet());
  }

  @JsonIgnore
  public Map<String, String> getTableIdStreamIdMap(Set<String> tableIds) {
    Set<XClusterTableConfig> tableConfigs = getTablesById(tableIds);
    Map<String, String> tableIdStreamIdMap = new HashMap<>();
    tableConfigs.forEach(
        tableConfig -> tableIdStreamIdMap.put(tableConfig.tableId, tableConfig.streamId));
    return tableIdStreamIdMap;
  }

  @Transactional
  public void setReplicationSetupDone(Collection<String> tableIds, boolean replicationSetupDone) {
    // Ensure there is no duplicate in the tableIds collection.
    if (tableIds.size() != new HashSet<>(tableIds).size()) {
      String errMsg = String.format("There are duplicate values in tableIds: %s", tableIds);
      throw new RuntimeException(errMsg);
    }
    for (String tableId : tableIds) {
      Optional<XClusterTableConfig> tableConfig = maybeGetTableById(tableId);
      if (tableConfig.isPresent()) {
        tableConfig.get().replicationSetupDone = replicationSetupDone;
      } else {
        String errMsg =
            String.format(
                "Could not find tableId (%s) in the xCluster config with uuid (%s)", tableId, uuid);
        throw new RuntimeException(errMsg);
      }
    }
    log.info(
        "Replication for tables {} in xCluster config {} is set to {}",
        tableIds,
        name,
        replicationSetupDone);
    update();
  }

  public void setReplicationSetupDone(Collection<String> tableIds) {
    setReplicationSetupDone(tableIds, true /* replicationSetupDone */);
  }

  @Transactional
  public void removeTables(Set<String> tableIds) {
    if (this.tables == null) {
      log.debug("No tables is set for xCluster config {}", this.uuid);
      return;
    }
    for (String tableId : tableIds) {
      if (!this.tables.removeIf(tableConfig -> tableConfig.tableId.equals(tableId))) {
        log.debug(
            "Table with id {} was not found to delete in xCluster config {}", tableId, this.uuid);
      }
    }
    update();
  }

  @Transactional
  public void setBackupForTables(Set<String> tableIds, Backup backup) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.backup = backup);
    update();
  }

  @Transactional
  public void setRestoreForTables(Set<String> tableIds, Restore restore) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.restore = restore);
    update();
  }

  @Transactional
  public void setRestoreTimeForTables(Set<String> tableIds, Date restoreTime, UUID taskUUID) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(
            tableConfig -> {
              tableConfig.restoreTime = restoreTime;
              tableConfig.restore.update(taskUUID, Restore.State.Completed);
            });
    update();
  }

  @Transactional
  public void setNeedBootstrapForTables(Collection<String> tableIds, boolean needBootstrap) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.needBootstrap = needBootstrap);
    update();
  }

  @Transactional
  public void setIndexTableForTables(Collection<String> tableIds, boolean indexTable) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.indexTable = indexTable);
    update();
  }

  @Transactional
  public void setBootstrapCreateTimeForTables(Collection<String> tableIds, Date moment) {
    ensureTableIdsExist(new HashSet<>(tableIds));
    this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.bootstrapCreateTime = moment);
    update();
  }

  @Transactional
  public void setStatusForTables(Collection<String> tableIds, XClusterTableConfig.Status status) {
    ensureTableIdsExist(new HashSet<>(tableIds));
    this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.status = status);
    update();
  }

  @JsonIgnore
  public Set<String> getTableIdsInStatus(
      Collection<String> tableIds, XClusterTableConfig.Status status) {
    return getTableIdsInStatus(tableIds, Collections.singleton(status));
  }

  @JsonIgnore
  public Set<String> getTableIdsInStatus(
      Collection<String> tableIds, Collection<XClusterTableConfig.Status> statuses) {
    ensureTableIdsExist(new HashSet<>(tableIds));
    return this.getTableDetails(true /* includeTxnTableIfExists */)
        .stream()
        .filter(
            tableConfig ->
                tableIds.contains(tableConfig.tableId) && statuses.contains(tableConfig.status))
        .map(tableConfig -> tableConfig.tableId)
        .collect(Collectors.toSet());
  }

  public String getReplicationGroupName() {
    return replicationGroupName;
  }

  public static String getReplicationGroupName(UUID sourceUniverseUUID, String configName) {
    return sourceUniverseUUID + "_" + configName;
  }

  @JsonIgnore
  public void setReplicationGroupName() {
    setReplicationGroupName(this.sourceUniverseUUID, this.name);
  }

  public void setReplicationGroupName(
      @JsonProperty("sourceUniverseUUID") UUID sourceUniverseUUID,
      @JsonProperty("name") String configName) {
    replicationGroupName = getReplicationGroupName(sourceUniverseUUID, configName);
  }

  public void setStatus(XClusterConfigStatusType status) {
    this.status = status;
    update();
  }

  public void enable() {
    if (!paused) {
      log.info("xCluster config {} is already enabled", this);
    }
    paused = false;
    update();
  }

  public void disable() {
    if (paused) {
      log.info("xCluster config {} is already disabled", this);
    }
    paused = true;
    update();
  }

  public void setPaused(boolean paused) {
    if (paused) {
      disable();
    } else {
      enable();
    }
  }

  public void reset() {
    this.status = XClusterConfigStatusType.Initialized;
    this.paused = false;
    this.tables.forEach(tableConfig -> tableConfig.restoreTime = null);
    this.update();
  }

  @Transactional
  public static XClusterConfig create(
      String name,
      UUID sourceUniverseUUID,
      UUID targetUniverseUUID,
      XClusterConfigStatusType status) {
    XClusterConfig xClusterConfig = new XClusterConfig();
    xClusterConfig.uuid = UUID.randomUUID();
    xClusterConfig.name = name;
    xClusterConfig.sourceUniverseUUID = sourceUniverseUUID;
    xClusterConfig.targetUniverseUUID = targetUniverseUUID;
    xClusterConfig.status = status;
    xClusterConfig.paused = false;
    xClusterConfig.createTime = new Date();
    xClusterConfig.modifyTime = new Date();
    xClusterConfig.tableType = TableType.UNKNOWN;
    xClusterConfig.setReplicationGroupName();
    xClusterConfig.type = ConfigType.Basic;
    // Set the following variables to their default value. They will be only used for txn
    // xCluster configs.
    xClusterConfig.sourceActive =
        XClusterConfigTaskBase.TRANSACTION_SOURCE_UNIVERSE_ROLE_ACTIVE_DEFAULT;
    xClusterConfig.targetActive =
        XClusterConfigTaskBase.TRANSACTION_TARGET_UNIVERSE_ROLE_ACTIVE_DEFAULT;
    xClusterConfig.save();
    return xClusterConfig;
  }

  @Transactional
  public static XClusterConfig create(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    return create(
        name, sourceUniverseUUID, targetUniverseUUID, XClusterConfigStatusType.Initialized);
  }

  public static XClusterConfig create(
      String name,
      UUID sourceUniverseUUID,
      UUID targetUniverseUUID,
      ConfigType type,
      @Nullable Set<String> tableIds,
      @Nullable Set<String> tableIdsToBootstrap) {
    XClusterConfig xClusterConfig =
        create(name, sourceUniverseUUID, targetUniverseUUID, XClusterConfigStatusType.Initialized);
    // The default type is Basic. If it is set to be txn, then save it in the object.
    if (Objects.equals(type, ConfigType.Txn)) {
      xClusterConfig.type = ConfigType.Txn;
      xClusterConfig.txnTableReplicationGroupName =
          XClusterConfigTaskBase.TRANSACTION_STATUS_TABLE_REPLICATION_GROUP_NAME;
    }
    if (Objects.nonNull(tableIds) && Objects.nonNull(tableIdsToBootstrap)) {
      xClusterConfig.setTables(tableIds, tableIdsToBootstrap);
    } else if (Objects.nonNull(tableIds)) {
      xClusterConfig.setTables(tableIds);
    }
    return xClusterConfig;
  }

  public static XClusterConfig create(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID, ConfigType type) {
    return create(
        name,
        sourceUniverseUUID,
        targetUniverseUUID,
        type,
        null /* tableIds */,
        null /* tableIdsToBootstrap */);
  }

  @Transactional
  public static XClusterConfig create(XClusterConfigCreateFormData createFormData) {
    return createFormData.bootstrapParams == null
        ? create(
            createFormData.name,
            createFormData.sourceUniverseUUID,
            createFormData.targetUniverseUUID,
            createFormData.configType,
            createFormData.tables,
            null /* tableIdsToBootstrap */)
        : create(
            createFormData.name,
            createFormData.sourceUniverseUUID,
            createFormData.targetUniverseUUID,
            createFormData.configType,
            createFormData.tables,
            createFormData.bootstrapParams.tables);
  }

  @Transactional
  public static XClusterConfig create(
      XClusterConfigCreateFormData createFormData,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      Set<String> indexTableIdSet) {
    XClusterConfig xClusterConfig = create(createFormData);
    xClusterConfig.setTableType(requestedTableInfoList);
    if (xClusterConfig.type.equals(ConfigType.Txn)) {
      xClusterConfig.setTxnTableId(requestedTableInfoList);
      // We always to check whether bootstrapping is required for txn table.
      xClusterConfig.txnTableConfig.needBootstrap = true;
    }
    xClusterConfig.setIndexTableForTables(indexTableIdSet, true /* indexTable */);
    return xClusterConfig;
  }

  @VisibleForTesting
  @Transactional
  public static XClusterConfig create(
      XClusterConfigCreateFormData createFormData, XClusterConfigStatusType status) {
    XClusterConfig xClusterConfig = create(createFormData);
    xClusterConfig.setStatus(status);
    if (status == XClusterConfigStatusType.Running) {
      xClusterConfig.setReplicationSetupDone(createFormData.tables);
    }
    return xClusterConfig;
  }

  @Override
  public void update() {
    this.modifyTime = new Date();

    // Auto orphanRemoval is disabled for the children tables. Manually delete the orphan tables.
    SqlUpdate orphanRemovalSqlQuery =
        Ebean.createSqlUpdate("DELETE FROM xcluster_table_config WHERE config_uuid = :uuid");
    Set<XClusterTableConfig> allTableConfigs =
        this.getTableDetails(true /* includeTxnTableIfExists */);
    if (!allTableConfigs.isEmpty()) {
      orphanRemovalSqlQuery =
          Ebean.createSqlUpdate(
              "DELETE FROM xcluster_table_config WHERE config_uuid = :uuid "
                  + "AND NOT ( table_id IN (:nonOrphanTableIds) )");
      orphanRemovalSqlQuery.setParameter(
          "nonOrphanTableIds",
          allTableConfigs
              .stream()
              .map(tableConfig -> tableConfig.tableId)
              .collect(Collectors.toList()));
    }
    int numRowsDeleted = orphanRemovalSqlQuery.setParameter("uuid", this.uuid).execute();
    log.trace(
        "{} rows got deleted from xcluster_table_config table because they were removed "
            + "from the xClusterConfig object",
        numRowsDeleted);

    super.update();
  }

  @Override
  public boolean delete() {
    // Remove the txnTable from xcluster_table_config table.
    if (Objects.nonNull(this.getTxnTableDetails())) {
      this.txnTableConfig = null;
      super.update();
    }
    return super.delete();
  }

  public static XClusterConfig getValidConfigOrBadRequest(
      Customer customer, UUID xClusterConfigUUID) {
    XClusterConfig xClusterConfig = getOrBadRequest(xClusterConfigUUID);
    checkXClusterConfigInCustomer(xClusterConfig, customer);
    return xClusterConfig;
  }

  public static XClusterConfig getOrBadRequest(UUID xClusterConfigUUID) {
    return maybeGet(xClusterConfigUUID)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST, "Cannot find XClusterConfig " + xClusterConfigUUID));
  }

  public static Optional<XClusterConfig> maybeGet(UUID xClusterConfigUUID) {
    XClusterConfig xClusterConfig =
        find.query().fetch("tables").where().eq("uuid", xClusterConfigUUID).findOne();
    if (xClusterConfig == null) {
      log.info("Cannot find XClusterConfig {}", xClusterConfigUUID);
      return Optional.empty();
    }
    // Phony call to force ORM to load the txnTableConfig object eagerly. It looks like an Ebean bug
    // because although Eagerly fetch is selected, it still loads the object lazily.
    if (Objects.nonNull(xClusterConfig.txnTableConfig)) {
      xClusterConfig.txnTableConfig.getBackupUuid();
    }
    return Optional.of(xClusterConfig);
  }

  public static List<XClusterConfig> getByTargetUniverseUUID(UUID targetUniverseUUID) {
    List<XClusterConfig> xClusterConfigs =
        find.query()
            .fetch("tables")
            .where()
            .eq("target_universe_uuid", targetUniverseUUID)
            .findList();
    // Phony call to force ORM to load the txnTableConfig object eagerly. It looks like an Ebean bug
    // because although Eagerly fetch is selected, it still loads the object lazily.
    xClusterConfigs.forEach(
        xClusterConfig -> {
          if (Objects.nonNull(xClusterConfig.txnTableConfig)) {
            xClusterConfig.txnTableConfig.getBackupUuid();
          }
        });
    return xClusterConfigs;
  }

  public static List<XClusterConfig> getBySourceUniverseUUID(UUID sourceUniverseUUID) {
    List<XClusterConfig> xClusterConfigs =
        find.query()
            .fetch("tables")
            .where()
            .eq("source_universe_uuid", sourceUniverseUUID)
            .findList();
    // Phony call to force ORM to load the txnTableConfig object eagerly. It looks like an Ebean bug
    // because although Eagerly fetch is selected, it still loads the object lazily.
    xClusterConfigs.forEach(
        xClusterConfig -> {
          if (Objects.nonNull(xClusterConfig.txnTableConfig)) {
            xClusterConfig.txnTableConfig.getBackupUuid();
          }
        });
    return xClusterConfigs;
  }

  public static List<XClusterConfig> getByUniverseUuid(UUID universeUuid) {
    return Stream.concat(
            getBySourceUniverseUUID(universeUuid).stream(),
            getByTargetUniverseUUID(universeUuid).stream())
        .collect(Collectors.toList());
  }

  public static List<XClusterConfig> getBetweenUniverses(
      UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    List<XClusterConfig> xClusterConfigs =
        find.query()
            .fetch("tables")
            .where()
            .eq("source_universe_uuid", sourceUniverseUUID)
            .eq("target_universe_uuid", targetUniverseUUID)
            .findList();
    // Phony call to force ORM to load the txnTableConfig object eagerly. It looks like an Ebean bug
    // because although Eagerly fetch is selected, it still loads the object lazily.
    xClusterConfigs.forEach(
        xClusterConfig -> {
          if (Objects.nonNull(xClusterConfig.txnTableConfig)) {
            xClusterConfig.txnTableConfig.getBackupUuid();
          }
        });
    return xClusterConfigs;
  }

  public static XClusterConfig getByNameSourceTarget(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    XClusterConfig xClusterConfig =
        find.query()
            .fetch("tables")
            .where()
            .eq("config_name", name)
            .eq("source_universe_uuid", sourceUniverseUUID)
            .eq("target_universe_uuid", targetUniverseUUID)
            .findOne();
    // Phony call to force ORM to load the txnTableConfig object eagerly. It looks like an Ebean bug
    // because although Eagerly fetch is selected, it still loads the object lazily.
    if (Objects.nonNull(xClusterConfig) && Objects.nonNull(xClusterConfig.txnTableConfig)) {
      xClusterConfig.txnTableConfig.getBackupUuid();
    }
    return xClusterConfig;
  }

  private static void checkXClusterConfigInCustomer(
      XClusterConfig xClusterConfig, Customer customer) {
    Set<UUID> customerUniverseUUIDs = customer.getUniverseUUIDs();
    if ((xClusterConfig.sourceUniverseUUID != null
            && !customerUniverseUUIDs.contains(xClusterConfig.sourceUniverseUUID))
        || (xClusterConfig.targetUniverseUUID != null
            && !customerUniverseUUIDs.contains(xClusterConfig.targetUniverseUUID))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "XClusterConfig %s doesn't belong to Customer %s",
              xClusterConfig.uuid, customer.uuid));
    }
  }

  private void addTableConfig(XClusterTableConfig tableConfig) {
    if (!this.tables.add(tableConfig)) {
      log.debug(
          "Table with id {} already exists in xCluster config ({})",
          tableConfig.tableId,
          this.uuid);
    }
  }

  public void ensureTableIdsExist(Set<String> tableIds) {
    if (tableIds.isEmpty()) {
      return;
    }
    Set<String> tableIdsInXClusterConfig = getTableIds(true /* includeTxnTableIfExists */);
    tableIds.forEach(
        tableId -> {
          if (!tableIdsInXClusterConfig.contains(tableId)) {
            throw new RuntimeException(
                String.format(
                    "Could not find tableId (%s) in the xCluster config with uuid (%s)",
                    tableId, this.uuid));
          }
        });
  }

  public void ensureTableIdsExist(Collection<String> tableIds) {
    if (tableIds.isEmpty()) {
      return;
    }
    Set<String> tableIdSet = new HashSet<>(tableIds);
    // Ensure there is no duplicate in the tableIds collection.
    if (tableIds.size() != tableIdSet.size()) {
      String errMsg = String.format("There are duplicate values in tableIds: %s", tableIds);
      throw new RuntimeException(errMsg);
    }
    ensureTableIdsExist(tableIdSet);
  }

  public static <T> Set<T> intersectionOf(Set<T> firstSet, Set<T> secondSet) {
    if (firstSet == null || secondSet == null) {
      return new HashSet<>();
    }
    Set<T> intersection = new HashSet<>(firstSet);
    intersection.retainAll(secondSet);
    return intersection;
  }
}
