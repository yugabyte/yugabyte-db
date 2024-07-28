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
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbEnumValue;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.CascadeType;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.JoinTable;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.OneToMany;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;

@Slf4j
@Entity
@ApiModel(description = "xcluster config object")
@Getter
@Setter
public class XClusterConfig extends Model {

  public static final BiMap<TableType, CommonTypes.TableType>
      XClusterConfigTableTypeCommonTypesTableTypeBiMap =
          ImmutableBiMap.of(
              TableType.YSQL,
              CommonTypes.TableType.PGSQL_TABLE_TYPE,
              TableType.YCQL,
              CommonTypes.TableType.YQL_TABLE_TYPE);

  private static final Finder<UUID, XClusterConfig> find = new Finder<>(XClusterConfig.class) {};

  @Id
  @ApiModelProperty(value = "XCluster config UUID")
  private UUID uuid;

  @Column(name = "config_name")
  @ApiModelProperty(value = "XCluster config name")
  private String name;

  @ManyToOne
  @JoinColumn(name = "source_universe_uuid", referencedColumnName = "universe_uuid")
  @ApiModelProperty(value = "Source Universe UUID")
  private UUID sourceUniverseUUID;

  @ManyToOne
  @JoinColumn(name = "target_universe_uuid", referencedColumnName = "universe_uuid")
  @ApiModelProperty(value = "Target Universe UUID")
  private UUID targetUniverseUUID;

  @ApiModelProperty(
      value = "Status",
      allowableValues = "Initialized, Running, Updating, DeletedUniverse, DeletionFailed, Failed")
  private XClusterConfigStatusType status;

  /**
   * In the application logic, <em>NEVER<em/> read from the following variable. This is only used
   * for UI purposes.
   */
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "The replication status of the source universe; used for disaster recovery")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  private SourceUniverseState sourceUniverseState;

  /**
   * In the application logic, <em>NEVER<em/> read from the following variable. This is only used
   * for UI purposes.
   */
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "The replication status of the target universe; used for disaster recovery")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  private TargetUniverseState targetUniverseState;

  /**
   * In the application logic, <em>NEVER<em/> read from the following variable. This is only used
   * for UI purposes.
   */
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. The keyspace name that the xCluster"
              + " task is working on; used for disaster recovery")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  private String keyspacePending;

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
  private TableType tableType;

  @ApiModelProperty(value = "Whether this xCluster replication config is paused")
  private boolean paused;

  @ApiModelProperty(
      value = "YbaApi Internal. Whether this xCluster replication config was imported")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.18.0.0")
  private boolean imported;

  @ApiModelProperty(value = "Create time of the xCluster config", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date createTime;

  @ApiModelProperty(
      value = "Last modify time of the xCluster config",
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date modifyTime;

  @OneToMany(mappedBy = "config", cascade = CascadeType.ALL, orphanRemoval = true)
  private Set<XClusterTableConfig> tables = new HashSet<>();

  @OneToMany(mappedBy = "config", cascade = CascadeType.ALL, orphanRemoval = true)
  private Set<XClusterNamespaceConfig> namespaces = new HashSet<>();

  @ApiModelProperty(value = "Replication group name in the target universe cluster config")
  private String replicationGroupName;

  public enum ConfigType {
    Basic,
    Txn,
    Db;

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
  private ConfigType type;

  @ApiModelProperty(value = "Whether the source is active in txn xCluster")
  private boolean sourceActive;

  @ApiModelProperty(value = "Whether the target is active in txn xCluster")
  private boolean targetActive;

  @ManyToOne(cascade = {CascadeType.PERSIST, CascadeType.MERGE, CascadeType.REFRESH})
  @JoinColumn(name = "dr_config_uuid", referencedColumnName = "uuid")
  @JsonIgnore
  private DrConfig drConfig;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "Whether this xCluster config is used as a secondary config for a DR config")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  private boolean secondary;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "The list of PITR configs used for the txn xCluster config")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.18.2.0")
  @OneToMany
  @JoinTable(
      name = "xcluster_pitr",
      joinColumns = @JoinColumn(name = "xcluster_uuid", referencedColumnName = "uuid"),
      inverseJoinColumns = @JoinColumn(name = "pitr_uuid", referencedColumnName = "uuid"))
  private List<PitrConfig> pitrConfigs;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "Whether the xCluster config is used as part of a DR config")
  @JsonProperty
  // Todo: Uncomment the following after YbaApi utest supports JsonProperty annotation.
  //  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public boolean isUsedForDr() {
    return maybeGetDrConfig().isPresent();
  }

  public void addPitrConfig(PitrConfig pitrConfig) {
    if (this.pitrConfigs.stream().noneMatch(p -> p.getUuid().equals(pitrConfig.getUuid()))) {
      this.pitrConfigs.add(pitrConfig);
      this.update();
    }
  }

  @Override
  public String toString() {
    return this.getReplicationGroupName()
        + "(uuid="
        + this.getUuid()
        + ",targetUuid="
        + this.getTargetUniverseUUID()
        + ",status="
        + this.getStatus()
        + ",paused="
        + this.isPaused()
        + ",tableType="
        + this.getTableType()
        + ",type="
        + this.getType()
        + ")";
  }

  public Optional<XClusterTableConfig> maybeGetTableById(String tableId) {
    // There will be at most one tableConfig for a tableId within each xCluster config.
    return this.getTableDetails().stream()
        .filter(tableConfig -> tableConfig.getTableId().equals(tableId))
        .findAny();
  }

  public Optional<XClusterNamespaceConfig> maybeGetNamespaceById(String namespaceId) {
    // There will be at most one namespaceConfig for a namespaceId within each xCluster config.
    return this.getNamespaceDetails().stream()
        .filter(namespaceConfig -> namespaceConfig.getSourceNamespaceId().equals(namespaceId))
        .findAny();
  }

  public Optional<DrConfig> maybeGetDrConfig() {
    return Optional.ofNullable(this.drConfig);
  }

  @JsonIgnore
  public CommonTypes.TableType getTableTypeAsCommonType() {
    if (getTableType().equals(TableType.UNKNOWN)) {
      throw new RuntimeException(
          "Table type is UNKNOWN, and cannot be mapped to CommonTypes.TableType");
    }
    return XClusterConfigTableTypeCommonTypesTableTypeBiMap.get(getTableType());
  }

  @JsonIgnore
  public CommonTypes.TableType updateTableType(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList) {
    if (!this.getTableType().equals(TableType.UNKNOWN)) {
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
    // All tables have the same type.
    if (!tableInfoList.stream()
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
    this.setTableType(
        XClusterConfigTableTypeCommonTypesTableTypeBiMap.inverse().get(typeAsCommonType));
    update();
    return typeAsCommonType;
  }

  public static List<XClusterConfig> getAllXClusterConfigs() {
    return find.query().findList();
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
        this.getTableDetails().stream()
            .collect(
                Collectors.toMap(
                    tableConfig -> tableConfig.getTableId(), tableConfig -> tableConfig));
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

  @ApiModelProperty(value = "Tables participating in this xCluster config")
  @JsonProperty("tableDetails")
  public Set<XClusterTableConfig> getTableDetails() {
    return tables.stream()
        .sorted(Comparator.comparing(XClusterTableConfig::getStatus))
        .collect(Collectors.toCollection(LinkedHashSet::new));
  }

  @ApiModelProperty(value = "Namespaces participating in this xCluster config")
  @JsonProperty("namespaceDetails")
  public Set<XClusterNamespaceConfig> getNamespaceDetails() {
    return namespaces.stream()
        .sorted(Comparator.comparing(XClusterNamespaceConfig::getStatus))
        .collect(Collectors.toCollection(LinkedHashSet::new));
  }

  @JsonProperty("tables")
  public Set<String> getTableIds() {
    return this.tables.stream()
        .sorted(Comparator.comparing(XClusterTableConfig::getStatus))
        .map(XClusterTableConfig::getTableId)
        .collect(Collectors.toCollection(LinkedHashSet::new));
  }

  @JsonProperty("dbs")
  public Set<String> getDbIds() {
    return this.namespaces.stream()
        .map(XClusterNamespaceConfig::getSourceNamespaceId)
        .collect(Collectors.toCollection(LinkedHashSet::new));
  }

  @JsonIgnore
  public Set<String> getTableIdsWithReplicationSetup(Set<String> tableIds, boolean done) {
    return this.getTableDetails().stream()
        .filter(
            table ->
                tableIds.contains(table.getTableId()) && table.isReplicationSetupDone() == done)
        .map(table -> table.getTableId())
        .collect(Collectors.toSet());
  }

  @JsonIgnore
  public Set<String> getTableIdsWithReplicationSetup(boolean done) {
    return getTableIdsWithReplicationSetup(getTableIds(), done);
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
      return this.getTableIds();
    }
    return this.getTables().stream()
        .filter(table -> table.isIndexTable() == includeIndexTables)
        .map(table -> table.getTableId())
        .collect(Collectors.toSet());
  }

  @JsonIgnore
  public Set<String> getTableIdsExcludeIndexTables() {
    return getTableIds(true /* includeMainTables */, false /* includeIndexTables */);
  }

  public void updateTables(Set<String> tableIds) {
    updateTables(tableIds, null /* tableIdsNeedBootstrap */);
  }

  @Transactional
  public void updateTables(Set<String> tableIds, Set<String> tableIdsNeedBootstrap) {
    this.getTables().clear();
    addTables(tableIds, tableIdsNeedBootstrap);
  }

  @Transactional
  public void updateNamespaces(Set<String> namespaceIds) {
    this.getNamespaces().clear();
    addNamespaces(namespaceIds);
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
    tableIds.forEach(
        tableId -> {
          XClusterTableConfig tableConfig = new XClusterTableConfig(this, tableId);
          if (tableIdsNeedBootstrap != null && tableIdsNeedBootstrap.contains(tableId)) {
            tableConfig.setNeedBootstrap(true);
          }
          addTableConfig(tableConfig);
        });
    update();
  }

  @Transactional
  public void addNamespaces(Set<String> namespaceIds) {
    for (String namespaceId : namespaceIds) {
      XClusterNamespaceConfig namespaceConfig = new XClusterNamespaceConfig(this, namespaceId);
      addNamespaceConfig(namespaceConfig);
    }
    update();
  }

  private void addNamespaceConfig(XClusterNamespaceConfig namespaceConfig) {
    if (!this.getNamespaces().add(namespaceConfig)) {
      log.debug(
          "Namespace with id {} already exists in xCluster config ({})",
          namespaceConfig.getSourceNamespaceId(),
          this.getUuid());
    }
  }

  @Transactional
  public void addTablesIfNotExist(
      Set<String> tableIds, Set<String> tableIdsNeedBootstrap, boolean areIndexTables) {
    if (tableIds.isEmpty()) {
      return;
    }
    Set<String> nonExistingTableIds =
        tableIds.stream()
            .filter(tableId -> !this.getTableIds().contains(tableId))
            .collect(Collectors.toSet());
    Set<String> nonExistingTableIdsNeedBootstrap = null;
    if (tableIdsNeedBootstrap != null) {
      nonExistingTableIdsNeedBootstrap =
          tableIdsNeedBootstrap.stream()
              .filter(nonExistingTableIds::contains)
              .collect(Collectors.toSet());
    }
    addTables(nonExistingTableIds, nonExistingTableIdsNeedBootstrap);
    if (areIndexTables) {
      this.updateIndexTableForTables(tableIds, true /* indexTable */);
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
    tableIdsStreamIdsMap.forEach(
        (tableId, streamId) -> {
          XClusterTableConfig tableConfig = new XClusterTableConfig(this, tableId);
          tableConfig.setStreamId(streamId);
          addTableConfig(tableConfig);
        });
    update();
  }

  @JsonIgnore
  public Set<String> getStreamIdsWithReplicationSetup() {
    return this.getTables().stream()
        .filter(XClusterTableConfig::isReplicationSetupDone)
        .map(XClusterTableConfig::getStreamId)
        .collect(Collectors.toSet());
  }

  @JsonIgnore
  public Map<String, String> getTableIdStreamIdMap(Set<String> tableIds) {
    Set<XClusterTableConfig> tableConfigs = getTablesById(tableIds);
    Map<String, String> tableIdStreamIdMap = new HashMap<>();
    tableConfigs.forEach(
        tableConfig -> tableIdStreamIdMap.put(tableConfig.getTableId(), tableConfig.getStreamId()));
    return tableIdStreamIdMap;
  }

  @Transactional
  public void updateReplicationSetupDone(
      Collection<String> tableIds, boolean replicationSetupDone) {
    // Ensure there is no duplicate in the tableIds collection.
    if (tableIds.size() != new HashSet<>(tableIds).size()) {
      String errMsg = String.format("There are duplicate values in tableIds: %s", tableIds);
      throw new RuntimeException(errMsg);
    }
    for (String tableId : tableIds) {
      Optional<XClusterTableConfig> tableConfig = maybeGetTableById(tableId);
      if (tableConfig.isPresent()) {
        tableConfig.get().setReplicationSetupDone(replicationSetupDone);
      } else {
        String errMsg =
            String.format(
                "Could not find tableId (%s) in the xCluster config with uuid (%s)",
                tableId, getUuid());
        throw new RuntimeException(errMsg);
      }
    }
    log.info(
        "Replication for tables {} in xCluster config {} is set to {}",
        tableIds,
        getName(),
        replicationSetupDone);
    update();
  }

  public void updateReplicationSetupDone(Collection<String> tableIds) {
    updateReplicationSetupDone(tableIds, true /* replicationSetupDone */);
  }

  /**
   * Synchronizes the tables in the xCluster config with the provided table and index table IDs.
   * Adds any missing tables and removes any extra tables.
   *
   * @param tableIds The set of table IDs to synchronize.
   * @param indexTableIds The set of index table IDs to synchronize.
   */
  @Transactional
  public void syncTables(Set<String> tableIds, Set<String> indexTableIds) {
    addTablesIfNotExist(tableIds, null, false);
    addTablesIfNotExist(indexTableIds, null, true);
    tableIds.addAll(indexTableIds);
    removeExtraTables(tableIds);
  }

  /**
   * Removes any extra tables from the xCluster config that are not in the provided set of table
   * IDs.
   *
   * @param tableIds The set of table IDs to keep in the xCluster config.
   */
  @Transactional
  public void removeExtraTables(Set<String> tableIds) {
    Set<String> extraTableIds =
        this.getTableIds().stream()
            .filter(tableId -> !tableIds.contains(tableId))
            .collect(Collectors.toSet());
    removeTables(extraTableIds);
  }

  @Transactional
  public void removeTables(Set<String> tableIds) {
    if (this.getTableIds() == null) {
      log.debug("No tables is set for xCluster config {}", this.getUuid());
      return;
    }
    for (String tableId : tableIds) {
      if (!this.getTables().removeIf(tableConfig -> tableConfig.getTableId().equals(tableId))) {
        log.debug(
            "Table with id {} was not found to delete in xCluster config {}",
            tableId,
            this.getUuid());
      }
    }
    update();
  }

  /**
   * Removes any namespaces from the xCluster config that are in the provided set of namespace IDs.
   *
   * @param namespaceIds The set of namespace IDs to keep in the xCluster config.
   */
  @Transactional
  public void removeNamespaces(Set<String> namespaceIds) {
    if (this.getDbIds() == null) {
      log.debug("No namespaces is set for xCluster config {}", this.getUuid());
      return;
    }
    for (String namespaceId : namespaceIds) {
      if (!this.getNamespaces()
          .removeIf(
              namespaceConfig -> namespaceConfig.getSourceNamespaceId().equals(namespaceId))) {
        log.debug(
            "Namespace with id {} was not found to delete in xCluster config {}",
            namespaceId,
            this.getUuid());
      }
    }
    update();
  }

  @Transactional
  public void updateBackupForTables(Set<String> tableIds, Backup backup) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails().stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.getTableId()))
        .forEach(tableConfig -> tableConfig.setBackup(backup));
    update();
  }

  @Transactional
  public void updateRestoreForTables(Set<String> tableIds, Restore restore) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails().stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.getTableId()))
        .forEach(tableConfig -> tableConfig.setRestore(restore));
    update();
  }

  @Transactional
  public void updateRestoreTimeForTables(Set<String> tableIds, Date restoreTime, UUID taskUUID) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails().stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.getTableId()))
        .forEach(
            tableConfig -> {
              tableConfig.setRestoreTime(restoreTime);
              tableConfig.getRestore().update(taskUUID, Restore.State.Completed);
            });
    update();
  }

  @Transactional
  public void updateNeedBootstrapForTables(Collection<String> tableIds, boolean needBootstrap) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails().stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.getTableId()))
        .forEach(tableConfig -> tableConfig.setNeedBootstrap(needBootstrap));
    update();
  }

  @Transactional
  public void updateIndexTableForTables(Collection<String> tableIds, boolean indexTable) {
    ensureTableIdsExist(tableIds);
    this.getTableDetails().stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.getTableId()))
        .forEach(tableConfig -> tableConfig.setIndexTable(indexTable));
    update();
  }

  public void updateIndexTablesFromMainTableIndexTablesMap(
      Map<String, List<String>> mainTableIndexTablesMap) {
    Set<String> tableIdsInConfig = this.getTableIds();
    mainTableIndexTablesMap.forEach(
        (mainTableId, indexTableIds) -> {
          if (this.maybeGetTableById(mainTableId).isPresent()) {
            List<String> indexTableIdsInConfig = new ArrayList<>(indexTableIds);
            indexTableIdsInConfig.retainAll(tableIdsInConfig);
            this.updateIndexTableForTables(indexTableIdsInConfig, true /* indexTable */);
          }
        });
  }

  @Transactional
  public void updateBootstrapCreateTimeForTables(Collection<String> tableIds, Date moment) {
    ensureTableIdsExist(new HashSet<>(tableIds));
    this.getTableDetails().stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.getTableId()))
        .forEach(tableConfig -> tableConfig.setBootstrapCreateTime(moment));
    update();
  }

  @Transactional
  public void updateStatusForTables(
      Collection<String> tableIds, XClusterTableConfig.Status status) {
    ensureTableIdsExist(new HashSet<>(tableIds));
    this.getTableDetails().stream()
        .filter(tableConfig -> tableIds.contains(tableConfig.getTableId()))
        .forEach(tableConfig -> tableConfig.setStatus(status));
    update();
  }

  @Transactional
  public void updateStatusForNamespaces(
      Collection<String> namespaceIds, XClusterNamespaceConfig.Status status) {
    ensureNamespaceIdsExist(new HashSet<>(namespaceIds));
    this.getNamespaceDetails().stream()
        .filter(namespaceConfig -> namespaceIds.contains(namespaceConfig.getSourceNamespaceId()))
        .forEach(namespaceConfig -> namespaceConfig.setStatus(status));
    update();
  }

  @Transactional
  public void updateStatusForNamespace(String namespaceId, XClusterNamespaceConfig.Status status) {
    Set<String> namespaceIds = new HashSet<>(Set.of(namespaceId));
    this.updateStatusForNamespaces(namespaceIds, status);
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
    return this.getTableDetails().stream()
        .filter(
            tableConfig ->
                tableIds.contains(tableConfig.getTableId())
                    && statuses.contains(tableConfig.getStatus()))
        .map(tableConfig -> tableConfig.getTableId())
        .collect(Collectors.toSet());
  }

  @JsonIgnore
  public Set<String> getNamespaceIdsInStatus(
      Collection<String> namespaceIds, Collection<XClusterNamespaceConfig.Status> statuses) {
    ensureNamespaceIdsExist(new HashSet<>(namespaceIds));
    return this.getNamespaceDetails().stream()
        .filter(
            namespaceConfig ->
                namespaceIds.contains(namespaceConfig.getSourceNamespaceId())
                    && statuses.contains(namespaceConfig.getStatus()))
        .map(namespaceConfig -> namespaceConfig.getSourceNamespaceId())
        .collect(Collectors.toSet());
  }

  @JsonIgnore
  public String getNewReplicationGroupName(UUID sourceUniverseUUID, String configName) {
    if (imported) {
      return configName;
    }
    return sourceUniverseUUID + "_" + configName;
  }

  public void setReplicationGroupName(String replicationGroupName) {
    if (imported) {
      this.replicationGroupName = replicationGroupName;
      return;
    }
    setReplicationGroupName(this.getSourceUniverseUUID(), replicationGroupName /* configName */);
  }

  @JsonIgnore
  public void setReplicationGroupName(UUID sourceUniverseUUID, String configName) {
    replicationGroupName = getNewReplicationGroupName(sourceUniverseUUID, configName);
  }

  public void updateStatus(XClusterConfigStatusType status) {
    this.setStatus(status);
    update();
  }

  public void enable() {
    if (!isPaused()) {
      log.info("xCluster config {} is already enabled", this);
    }
    setPaused(false);
    update();
  }

  public void disable() {
    if (isPaused()) {
      log.info("xCluster config {} is already disabled", this);
    }
    setPaused(true);
    update();
  }

  public void updatePaused(boolean paused) {
    if (paused) {
      disable();
    } else {
      enable();
    }
  }

  public void reset() {
    this.setStatus(XClusterConfigStatusType.Initialized);
    this.setPaused(false);
    this.getTables().forEach(tableConfig -> tableConfig.setRestoreTime(null));
    this.update();
  }

  @Transactional
  public static XClusterConfig create(
      String name,
      UUID sourceUniverseUUID,
      UUID targetUniverseUUID,
      XClusterConfigStatusType status,
      boolean imported) {
    XClusterConfig xClusterConfig = new XClusterConfig();
    xClusterConfig.setUuid(UUID.randomUUID());
    xClusterConfig.setName(name);
    xClusterConfig.setSourceUniverseUUID(sourceUniverseUUID);
    xClusterConfig.setTargetUniverseUUID(targetUniverseUUID);
    xClusterConfig.setStatus(status);
    // Imported needs to be set before setReplicationGroupName() call.
    xClusterConfig.setImported(imported);
    xClusterConfig.setPaused(false);
    xClusterConfig.setCreateTime(new Date());
    xClusterConfig.setModifyTime(new Date());
    xClusterConfig.setTableType(TableType.UNKNOWN);
    xClusterConfig.setType(ConfigType.Basic);
    // Set the following variables to their default value. They will be only used for txn
    // xCluster configs.
    xClusterConfig.setSourceActive(
        XClusterConfigTaskBase.TRANSACTION_SOURCE_UNIVERSE_ROLE_ACTIVE_DEFAULT);
    xClusterConfig.setTargetActive(
        XClusterConfigTaskBase.TRANSACTION_TARGET_UNIVERSE_ROLE_ACTIVE_DEFAULT);
    xClusterConfig.setReplicationGroupName(name);
    xClusterConfig.setSourceUniverseState(SourceUniverseState.Unconfigured);
    xClusterConfig.setTargetUniverseState(TargetUniverseState.Unconfigured);
    xClusterConfig.save();
    return xClusterConfig;
  }

  @Transactional
  public static XClusterConfig create(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    return create(
        name,
        sourceUniverseUUID,
        targetUniverseUUID,
        XClusterConfigStatusType.Initialized,
        false /* imported */);
  }

  public static XClusterConfig create(
      String name,
      UUID sourceUniverseUUID,
      UUID targetUniverseUUID,
      ConfigType type,
      @Nullable Set<String> tableIds,
      @Nullable Set<String> tableIdsToBootstrap,
      boolean imported) {
    XClusterConfig xClusterConfig =
        create(
            name,
            sourceUniverseUUID,
            targetUniverseUUID,
            XClusterConfigStatusType.Initialized,
            imported);
    // The default type is Basic. If it is set to be txn, then save it in the object.
    if (Objects.equals(type, ConfigType.Txn)) {
      xClusterConfig.setType(ConfigType.Txn);
    }
    if (Objects.nonNull(tableIds) && Objects.nonNull(tableIdsToBootstrap)) {
      xClusterConfig.updateTables(tableIds, tableIdsToBootstrap);
    } else if (Objects.nonNull(tableIds)) {
      xClusterConfig.updateTables(tableIds);
    }
    return xClusterConfig;
  }

  public static XClusterConfig create(
      String name,
      UUID sourceUniverseUUID,
      UUID targetUniverseUUID,
      ConfigType type,
      boolean imported) {
    return create(
        name,
        sourceUniverseUUID,
        targetUniverseUUID,
        type,
        null /* tableIds */,
        null /* tableIdsToBootstrap */,
        imported);
  }

  public static XClusterConfig create(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID, ConfigType type) {
    return create(name, sourceUniverseUUID, targetUniverseUUID, type, false /* imported */);
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
            null /* tableIdsToBootstrap */,
            false /* imported */)
        : create(
            createFormData.name,
            createFormData.sourceUniverseUUID,
            createFormData.targetUniverseUUID,
            createFormData.configType,
            createFormData.tables,
            createFormData.bootstrapParams.tables,
            false /* imported */);
  }

  @Transactional
  public static XClusterConfig create(
      XClusterConfigCreateFormData createFormData,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList) {
    XClusterConfig xClusterConfig = create(createFormData);
    xClusterConfig.updateTableType(requestedTableInfoList);
    return xClusterConfig;
  }

  @VisibleForTesting
  @Transactional
  public static XClusterConfig create(
      XClusterConfigCreateFormData createFormData, XClusterConfigStatusType status) {
    XClusterConfig xClusterConfig = create(createFormData);
    xClusterConfig.updateStatus(status);
    if (status == XClusterConfigStatusType.Running) {
      xClusterConfig.updateReplicationSetupDone(createFormData.tables);
    }
    return xClusterConfig;
  }

  @Override
  public void update() {
    this.setModifyTime(new Date());
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
        .fetch("drConfig")
        .fetch("pitrConfigs")
        .where()
        .eq("target_universe_uuid", targetUniverseUUID)
        .findList();
  }

  public static List<XClusterConfig> getBySourceUniverseUUID(UUID sourceUniverseUUID) {
    return find.query()
        .fetch("tables")
        .fetch("drConfig")
        .fetch("pitrConfigs")
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
        .fetch("drConfig")
        .fetch("pitrConfigs")
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

  public static XClusterConfig getByReplicationGroupNameTarget(
      String replicationGroupName, UUID targetUniverseUUID) {
    return find.query()
        .fetch("tables")
        .where()
        .eq("replication_group_name", replicationGroupName)
        .eq("target_universe_uuid", targetUniverseUUID)
        .findOne();
  }

  public static void checkXClusterConfigInCustomer(
      XClusterConfig xClusterConfig, Customer customer) {
    Set<UUID> customerUniverseUUIDs = customer.getUniverseUUIDs();
    if ((xClusterConfig.getSourceUniverseUUID() != null
            && !customerUniverseUUIDs.contains(xClusterConfig.getSourceUniverseUUID()))
        || (xClusterConfig.getTargetUniverseUUID() != null
            && !customerUniverseUUIDs.contains(xClusterConfig.getTargetUniverseUUID()))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "XClusterConfig %s doesn't belong to Customer %s",
              xClusterConfig.getUuid(), customer.getUuid()));
    }
  }

  public void addTableConfig(XClusterTableConfig tableConfig) {
    if (!this.getTables().add(tableConfig)) {
      log.debug(
          "Table with id {} already exists in xCluster config ({})",
          tableConfig.getTableId(),
          this.getUuid());
    }
  }

  public void ensureTableIdsExist(Set<String> tableIds) {
    if (tableIds.isEmpty()) {
      return;
    }
    Set<String> tableIdsInXClusterConfig = getTableIds();
    tableIds.forEach(
        tableId -> {
          if (!tableIdsInXClusterConfig.contains(tableId)) {
            throw new RuntimeException(
                String.format(
                    "Could not find tableId (%s) in the xCluster config with uuid (%s)",
                    tableId, this.getUuid()));
          }
        });
  }

  public void ensureNamespaceIdsExist(Set<String> namespaceIds) {
    if (namespaceIds.isEmpty()) {
      return;
    }
    Set<String> namespaceIdsInXClusterConfig = getDbIds();
    namespaceIds.forEach(
        namespaceId -> {
          if (!namespaceIdsInXClusterConfig.contains(namespaceId)) {
            throw new RuntimeException(
                String.format(
                    "Could not find namespaceId (%s) in the xCluster config with uuid (%s)",
                    namespaceId, this.getUuid()));
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

  public void ensureNamespaceIdsExist(Collection<String> namespaceIds) {
    if (namespaceIds.isEmpty()) {
      return;
    }
    Set<String> namespaceIdSet = new HashSet<>(namespaceIds);
    // Ensure there is no duplicate in the namespaceIds collection.
    if (namespaceIds.size() != namespaceIdSet.size()) {
      String errMsg = String.format("There are duplicate values in namespaceIds: %s", namespaceIds);
      throw new RuntimeException(errMsg);
    }
    ensureNamespaceIdsExist(namespaceIdSet);
  }

  public static <T> Set<T> intersectionOf(Set<T> firstSet, Set<T> secondSet) {
    if (firstSet == null || secondSet == null) {
      return new HashSet<>();
    }
    Set<T> intersection = new HashSet<>(firstSet);
    intersection.retainAll(secondSet);
    return intersection;
  }

  public static boolean isUniverseXClusterParticipant(UUID universeUUID) {
    return !CollectionUtils.isEmpty(getByUniverseUuid(universeUUID));
  }
}
