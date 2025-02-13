package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.DrConfigCreateForm.PitrParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.TableType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.CascadeType;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.OneToMany;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Entity
@ApiModel(description = "disaster recovery config object")
@Getter
@Setter
public class DrConfig extends Model {

  private static final Finder<UUID, DrConfig> find = new Finder<>(DrConfig.class) {};
  private static final Finder<UUID, XClusterConfig> findXClusterConfig =
      new Finder<>(XClusterConfig.class) {};

  @Id
  @ApiModelProperty(value = "DR config UUID")
  private UUID uuid;

  @ApiModelProperty(value = "Disaster recovery config name")
  private String name;

  @ApiModelProperty(value = "Create time of the DR config", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date createTime;

  @ApiModelProperty(value = "Last modify time of the DR config", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date modifyTime;

  @OneToMany(mappedBy = "drConfig", cascade = CascadeType.ALL)
  @JsonIgnore
  private List<XClusterConfig> xClusterConfigs;

  @ManyToOne
  @JoinColumn(name = "storage_config_uuid", referencedColumnName = "config_uuid")
  @JsonIgnore
  private UUID storageConfigUuid;

  @JsonIgnore private int parallelism;

  /**
   * In the application logic, <em>NEVER<em/> read from the following variable. This is only used
   * for UI purposes.
   */
  @ApiModelProperty(value = "The state of the DR config")
  private State state;

  @ApiModelProperty(value = "PITR Retention Period in seconds", accessMode = READ_WRITE)
  private Long pitrRetentionPeriodSec;

  @ApiModelProperty(value = "PITR Retention Period in seconds", accessMode = READ_WRITE)
  private Long pitrSnapshotIntervalSec;

  @Transactional
  public static DrConfig create(
      String name,
      UUID sourceUniverseUUID,
      UUID targetUniverseUUID,
      Set<String> tableIds,
      BootstrapParams.BootstrapBackupParams bootstrapBackupParams,
      PitrParams pitrParams) {
    DrConfig drConfig = new DrConfig();
    drConfig.name = name;
    drConfig.setCreateTime(new Date());
    drConfig.setModifyTime(new Date());
    drConfig.setState(State.Initializing);
    drConfig.setStorageConfigUuid(bootstrapBackupParams.storageConfigUUID);
    drConfig.setParallelism(bootstrapBackupParams.parallelism);
    drConfig.setPitrRetentionPeriodSec(pitrParams.retentionPeriodSec);
    drConfig.setPitrSnapshotIntervalSec(pitrParams.snapshotIntervalSec);

    // Create a corresponding xCluster object.
    XClusterConfig xClusterConfig =
        drConfig.addXClusterConfig(sourceUniverseUUID, targetUniverseUUID, ConfigType.Txn);
    xClusterConfig.updateTables(tableIds, tableIds /* tableIdsNeedBootstrap */);
    drConfig.save();
    return drConfig;
  }

  // For DB scoped replication.
  @Transactional
  public static DrConfig create(
      String name,
      UUID sourceUniverseUUID,
      UUID targetUniverseUUID,
      BootstrapParams.BootstrapBackupParams bootstrapBackupParams,
      PitrParams pitrParams,
      Set<String> sourceNamespaceIds) {
    DrConfig drConfig = new DrConfig();
    drConfig.name = name;
    drConfig.setCreateTime(new Date());
    drConfig.setModifyTime(new Date());
    drConfig.setState(State.Initializing);
    drConfig.setStorageConfigUuid(bootstrapBackupParams.storageConfigUUID);
    drConfig.setParallelism(bootstrapBackupParams.parallelism);
    drConfig.setPitrRetentionPeriodSec(pitrParams.retentionPeriodSec);
    drConfig.setPitrSnapshotIntervalSec(pitrParams.snapshotIntervalSec);

    XClusterConfig xClusterConfig =
        drConfig.addXClusterConfig(sourceUniverseUUID, targetUniverseUUID, ConfigType.Db);
    xClusterConfig.updateNamespaces(sourceNamespaceIds);
    drConfig.save();
    return drConfig;
  }

  /** It updates the dr config object and all of its xCluster configs in a transaction. */
  @Transactional
  public void update() {
    super.update();
  }

  public XClusterConfig addXClusterConfig(UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    return addXClusterConfig(sourceUniverseUUID, targetUniverseUUID, ConfigType.Txn);
  }

  public XClusterConfig addXClusterConfig(
      UUID sourceUniverseUUID, UUID targetUniverseUUID, ConfigType type) {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            this.getNewXClusterConfigName(sourceUniverseUUID, targetUniverseUUID),
            sourceUniverseUUID,
            targetUniverseUUID,
            XClusterConfigStatusType.Initialized,
            false /* imported */);
    xClusterConfig.setDrConfig(this);
    this.xClusterConfigs.add(xClusterConfig);
    // Dr only supports ysql tables.
    xClusterConfig.setTableType(TableType.YSQL);
    // Dr is only based on transactional or db scoped replication.
    xClusterConfig.setType(type);
    xClusterConfig.update();
    this.setModifyTime(new Date());

    return xClusterConfig;
  }

  @JsonIgnore
  public XClusterConfig getActiveXClusterConfig() {
    if (xClusterConfigs.isEmpty()) {
      throw new IllegalStateException(
          String.format(
              "DrConfig %s(%s) does not have any corresponding xCluster config",
              this.name, this.uuid));
    }

    return xClusterConfigs.stream()
        .filter(xClusterConfig -> !xClusterConfig.isSecondary())
        .findFirst()
        .orElseThrow(() -> new IllegalStateException("No active xCluster config found"));
  }

  public boolean hasActiveXClusterConfig() {
    if (xClusterConfigs.isEmpty()) {
      return false;
    }

    return xClusterConfigs.stream().anyMatch(config -> !config.isSecondary());
  }

  @JsonIgnore
  public Optional<XClusterConfig> getActiveXClusterConfig(
      UUID sourceUniverseUuid, UUID targetUniverseUuid) {
    if (xClusterConfigs.size() == 1) {
      return Optional.of(xClusterConfigs.get(0));
    }
    // For a DR config, there could be at most one xCluster config from universe A to universe B.
    List<XClusterConfig> xClusterConfigsFromSourceToTarget =
        xClusterConfigs.stream()
            .filter(
                xClusterConfig ->
                    xClusterConfig.getSourceUniverseUUID().equals(sourceUniverseUuid)
                        && xClusterConfig.getTargetUniverseUUID().equals(targetUniverseUuid))
            .toList();
    if (xClusterConfigsFromSourceToTarget.size() > 1) {
      throw new IllegalStateException(
          String.format(
              "DrConfig %s(%s) has more than one xCluster config from source universe %s to target"
                  + " universe %s",
              this.name, this.uuid, sourceUniverseUuid, targetUniverseUuid));
    }
    if (xClusterConfigsFromSourceToTarget.isEmpty()) {
      return Optional.empty();
    }
    return Optional.of(xClusterConfigsFromSourceToTarget.get(0));
  }

  @JsonIgnore
  public XClusterConfig getFailoverXClusterConfig() {
    return xClusterConfigs.stream().filter(XClusterConfig::isSecondary).findFirst().orElse(null);
  }

  public String getNewXClusterConfigName(UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    return "--DR-CONFIG-" + this.name + "--_" + UUID.randomUUID();
  }

  @Override
  public String toString() {
    return this.name + "(uuid=" + this.getUuid() + ")";
  }

  public static DrConfig getValidConfigOrBadRequest(Customer customer, UUID drConfigUuid) {
    DrConfig drConfig = getOrBadRequest(drConfigUuid);
    drConfig.xClusterConfigs.forEach(
        xClusterConfig -> XClusterConfig.checkXClusterConfigInCustomer(xClusterConfig, customer));
    return drConfig;
  }

  public static DrConfig getOrBadRequest(UUID drConfigUuid) {
    return maybeGet(drConfigUuid)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST, "Cannot find drConfig with uuid " + drConfigUuid));
  }

  public static Optional<DrConfig> maybeGet(UUID drConfigUuid) {
    DrConfig drConfig =
        find.query().fetch("xClusterConfigs").where().eq("uuid", drConfigUuid).findOne();
    if (drConfig == null) {
      log.info("Cannot find drConfig {} with uuid ", drConfig);
      return Optional.empty();
    }
    return Optional.of(drConfig);
  }

  public static Optional<DrConfig> maybeGetByName(String drConfigName) {
    DrConfig drConfig =
        find.query().fetch("xClusterConfigs", "").where().eq("name", drConfigName).findOne();
    if (drConfig == null) {
      log.info("Cannot find drConfig {} with uuid ", drConfig);
      return Optional.empty();
    }
    return Optional.of(drConfig);
  }

  public static List<DrConfig> getBySourceUniverseUuid(UUID sourceUniverseUuid) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getBySourceUniverseUUID(sourceUniverseUuid);
    Set<UUID> drConfigUuidList =
        xClusterConfigs.stream()
            .filter(XClusterConfig::isUsedForDr)
            .map(xClusterConfig -> xClusterConfig.getDrConfig().getUuid())
            .collect(Collectors.toSet());
    List<DrConfig> drConfigs = new ArrayList<>();
    drConfigUuidList.forEach(
        drConfigUuid ->
            drConfigs.add(
                find.query().fetch("xClusterConfigs").where().eq("uuid", drConfigUuid).findOne()));
    return drConfigs;
  }

  public static List<DrConfig> getByTargetUniverseUuid(UUID targetUniverseUuid) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByTargetUniverseUUID(targetUniverseUuid);
    Set<UUID> drConfigUuidList =
        xClusterConfigs.stream()
            .filter(XClusterConfig::isUsedForDr)
            .map(xClusterConfig -> xClusterConfig.getDrConfig().getUuid())
            .collect(Collectors.toSet());
    List<DrConfig> drConfigs = new ArrayList<>();
    drConfigUuidList.forEach(
        drConfigUuid ->
            drConfigs.add(
                find.query().fetch("xClusterConfigs").where().eq("uuid", drConfigUuid).findOne()));
    return drConfigs;
  }

  public static List<DrConfig> getByUniverseUuid(UUID universeUuid) {
    return Stream.concat(
            getBySourceUniverseUuid(universeUuid).stream(),
            getByTargetUniverseUuid(universeUuid).stream())
        .collect(Collectors.toList());
  }

  public static List<DrConfig> getAll() {
    return find.query().findList();
  }

  public static List<DrConfig> getBetweenUniverses(
      UUID sourceUniverseUuid, UUID targetUniverseUuid) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getBetweenUniverses(sourceUniverseUuid, targetUniverseUuid);
    Set<UUID> drConfigUuidList =
        xClusterConfigs.stream()
            .filter(XClusterConfig::isUsedForDr)
            .map(xClusterConfig -> xClusterConfig.getDrConfig().getUuid())
            .collect(Collectors.toSet());
    List<DrConfig> drConfigs = new ArrayList<>();
    drConfigUuidList.forEach(
        drConfigUuid ->
            drConfigs.add(
                find.query().fetch("xClusterConfigs").where().eq("uuid", drConfigUuid).findOne()));
    return drConfigs;
  }

  public static List<DrConfig> getByStorageConfigUuid(UUID storageConfigUuid) {
    return find.query().where().eq("storageConfigUuid", storageConfigUuid).findList();
  }

  @JsonIgnore
  public XClusterConfigRestartFormData.RestartBootstrapParams getBootstrapBackupParams() {
    XClusterConfigRestartFormData.RestartBootstrapParams bootstrapParams =
        new XClusterConfigRestartFormData.RestartBootstrapParams();
    BootstrapParams.BootstrapBackupParams backupRequestParams =
        new BootstrapParams.BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = this.storageConfigUuid;
    backupRequestParams.parallelism = this.parallelism;
    bootstrapParams.backupRequestParams = backupRequestParams;
    return bootstrapParams;
  }

  @JsonIgnore
  public boolean isHalted() {
    return state == State.Halted;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    DrConfig drConfig = (DrConfig) o;
    return Objects.equals(uuid, drConfig.uuid);
  }

  @Override
  public int hashCode() {
    return Objects.hash(uuid);
  }
}
