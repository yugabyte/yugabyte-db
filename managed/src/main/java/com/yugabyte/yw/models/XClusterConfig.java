// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.BiMap;
import com.google.common.collect.ImmutableBiMap;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbEnumValue;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collection;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.persistence.CascadeType;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.JoinColumn;
import javax.persistence.ManyToOne;
import javax.persistence.OneToMany;
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

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(
      value = "Create time of the xCluster config",
      example = "2022-04-26 15:37:32.610000")
  public Date createTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(
      value = "Last modify time of the xCluster config",
      example = "2022-04-26 15:37:32.610000")
  public Date modifyTime;

  @OneToMany(mappedBy = "config", cascade = CascadeType.ALL, orphanRemoval = true)
  @ApiModelProperty(value = "Tables participating in this xCluster config")
  @JsonIgnore
  public Set<XClusterTableConfig> tables;

  @ApiModelProperty(value = "Replication group name in DB")
  private String replicationGroupName;

  @Override
  public String toString() {
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
    return this.tables
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
      log.warn("tableType for {} is already set; skip setting it", this);
      return getTableTypeAsCommonType();
    }
    CommonTypes.TableType typeAsCommonType = tableInfoList.get(0).getTableType();
    // All tables have the same type.
    if (!tableInfoList
        .stream()
        .allMatch(tableInfo -> tableInfo.getTableType().equals(typeAsCommonType))) {
      throw new IllegalArgumentException(
          "At least one table has a different type from others. "
              + "All tables in an xCluster config must have the same type. Please create separate "
              + "xCluster configs for different table types.");
    }
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
        this.tables
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

  public Set<XClusterTableConfig> getTableDetails() {
    return this.tables;
  }

  @JsonProperty
  public Set<String> getTables() {
    return this.tables.stream().map(table -> table.tableId).collect(Collectors.toSet());
  }

  @JsonIgnore
  public Set<String> getTableIdsWithReplicationSetup(boolean done) {
    return this.tables
        .stream()
        .filter(table -> table.replicationSetupDone == done)
        .map(table -> table.tableId)
        .collect(Collectors.toSet());
  }

  public Set<String> getTableIdsWithReplicationSetup() {
    return getTableIdsWithReplicationSetup(true /* done */);
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
  public void addTablesIfNotExist(Set<String> tableIds, Set<String> tableIdsNeedBootstrap) {
    if (tableIds.isEmpty()) {
      return;
    }
    Set<String> nonExistingTableIds =
        tableIds
            .stream()
            .filter(tableId -> !this.getTables().contains(tableId))
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
  }

  @Transactional
  public void addTablesIfNotExist(Set<String> tableIds) {
    addTablesIfNotExist(tableIds, null /* tableIdsNeedBootstrap */);
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
    this.tables
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.backup = backup);
    update();
  }

  @Transactional
  public void setRestoreTimeForTables(Set<String> tableIds, Date restoreTime) {
    ensureTableIdsExist(tableIds);
    this.tables
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.restoreTime = restoreTime);
    update();
  }

  @Transactional
  public void setNeedBootstrapForTables(Collection<String> tableIds, boolean needBootstrap) {
    ensureTableIdsExist(tableIds);
    this.tables
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.needBootstrap = needBootstrap);
    update();
  }

  @Transactional
  public void setIndexTableForTables(Collection<String> tableIds, boolean indexTable) {
    ensureTableIdsExist(tableIds);
    this.tables
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.indexTable = indexTable);
    update();
  }

  @Transactional
  public void setBootstrapCreateTimeForTables(Collection<String> tableIds, Date moment) {
    ensureTableIdsExist(new HashSet<>(tableIds));
    this.tables
        .stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.bootstrapCreateTime = moment);
    update();
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
    this.tableType = TableType.UNKNOWN;
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
    xClusterConfig.save();
    return xClusterConfig;
  }

  @Transactional
  public static XClusterConfig create(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    return create(
        name, sourceUniverseUUID, targetUniverseUUID, XClusterConfigStatusType.Initialized);
  }

  @Transactional
  public static XClusterConfig create(XClusterConfigCreateFormData createFormData) {
    XClusterConfig xClusterConfig =
        create(
            createFormData.name,
            createFormData.sourceUniverseUUID,
            createFormData.targetUniverseUUID);
    if (createFormData.bootstrapParams != null) {
      xClusterConfig.setTables(createFormData.tables, createFormData.bootstrapParams.tables);
    } else {
      xClusterConfig.setTables(createFormData.tables);
    }
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
    super.update();
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
    return Optional.of(xClusterConfig);
  }

  public static List<XClusterConfig> getByTargetUniverseUUID(UUID targetUniverseUUID) {
    return find.query()
        .fetch("tables")
        .where()
        .eq("target_universe_uuid", targetUniverseUUID)
        .findList();
  }

  public static List<XClusterConfig> getBySourceUniverseUUID(UUID sourceUniverseUUID) {
    return find.query()
        .fetch("tables")
        .where()
        .eq("source_universe_uuid", sourceUniverseUUID)
        .findList();
  }

  public static List<XClusterConfig> getByUniverseUuid(UUID universeUuid) {
    return Stream.concat(
            getBySourceUniverseUUID(universeUuid).stream(),
            getByTargetUniverseUUID(universeUuid).stream())
        .collect(Collectors.toList());
  }

  public static List<XClusterConfig> getBetweenUniverses(
      UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    return find.query()
        .fetch("tables")
        .where()
        .eq("source_universe_uuid", sourceUniverseUUID)
        .eq("target_universe_uuid", targetUniverseUUID)
        .findList();
  }

  public static XClusterConfig getByNameSourceTarget(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    return find.query()
        .fetch("tables")
        .where()
        .eq("config_name", name)
        .eq("source_universe_uuid", sourceUniverseUUID)
        .eq("target_universe_uuid", targetUniverseUUID)
        .findOne();
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
    Set<String> tableIdsInXClusterConfig = getTables();
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
