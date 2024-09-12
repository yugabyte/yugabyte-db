package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.TableType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

@ApiModel(description = "disaster recovery get response")
public class DrConfigGetResp {

  private final DrConfig drConfig;
  private final XClusterConfig xClusterConfig;

  public DrConfigGetResp(DrConfig drConfig, XClusterConfig xClusterConfig) {
    this.drConfig = drConfig;
    this.xClusterConfig = xClusterConfig;
  }

  @ApiModelProperty(value = "DR config UUID")
  public UUID getUuid() {
    return drConfig.getUuid();
  }

  @ApiModelProperty(value = "Disaster recovery config name")
  public String getName() {
    return drConfig.getName();
  }

  @ApiModelProperty(
      value = "Status",
      allowableValues = "Initialized, Running, Updating, DeletedUniverse, DeletionFailed, Failed")
  public XClusterConfigStatusType getStatus() {
    return xClusterConfig.getStatus();
  }

  @ApiModelProperty(value = "Primary Universe UUID")
  public UUID getPrimaryUniverseUuid() {
    return xClusterConfig.getSourceUniverseUUID();
  }

  @ApiModelProperty(value = "DR Replica Universe UUID")
  public UUID getDrReplicaUniverseUuid() {
    return xClusterConfig.getTargetUniverseUUID();
  }

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "The replication status of the primary universe.")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public SourceUniverseState getPrimaryUniverseState() {
    return xClusterConfig.getSourceUniverseState();
  }

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "The replication status of the dr replica universe.")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public TargetUniverseState getDrReplicaUniverseState() {
    return xClusterConfig.getTargetUniverseState();
  }

  @ApiModelProperty(value = "The state of the DR config")
  public State getState() {
    return drConfig.getState();
  }

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. The keyspace name that the "
              + "current task is working on.")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public String getKeyspacePending() {
    return xClusterConfig.getKeyspacePending();
  }

  @ApiModelProperty(
      value = "The type of tables that are being replicated",
      allowableValues = "UNKNOWN, YSQL, YCQL")
  public TableType getTableType() {
    return xClusterConfig.getTableType();
  }

  @ApiModelProperty(value = "Whether the config is basic, txn, or db scoped xCluster")
  public ConfigType getType() {
    return xClusterConfig.getType();
  }

  @ApiModelProperty(value = "Whether the underlying xCluster config is paused")
  public boolean isPaused() {
    return xClusterConfig.isPaused();
  }

  @ApiModelProperty(value = "Create time of the DR config", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date getCreateTime() {
    return drConfig.getCreateTime();
  }

  @ApiModelProperty(value = "Last modify time of the DR config", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date getModifyTime() {
    return drConfig.getModifyTime();
  }

  @ApiModelProperty(value = "Bootstrap backup params for DR config")
  public XClusterConfigRestartFormData.RestartBootstrapParams getBootstrapParams() {
    return drConfig.getBootstrapBackupParams();
  }

  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. PITR Retention Period in seconds")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Long getPitrRetentionPeriodSec() {
    return drConfig.getPitrRetentionPeriodSec();
  }

  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. PITR Retention Period in seconds")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Long getPitrSnapshotIntervalSec() {
    return drConfig.getPitrSnapshotIntervalSec();
  }

  @ApiModelProperty(value = "Replication group name in the dr replica universe cluster config")
  public String getReplicationGroupName() {
    return xClusterConfig.getReplicationGroupName();
  }

  @ApiModelProperty(value = "Whether the primary universe is active")
  public boolean isPrimaryUniverseActive() {
    return xClusterConfig.isSourceActive();
  }

  @ApiModelProperty(value = "Whether the dr replica universe is active")
  public boolean isDrReplicaUniverseActive() {
    return xClusterConfig.isTargetActive();
  }

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "The list of PITR configs used for the underlying txn xCluster config")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.18.2.0")
  public List<PitrConfig> getPitrConfigs() {
    return xClusterConfig.getPitrConfigs();
  }

  @ApiModelProperty(value = "Details for each table in replication")
  public Set<XClusterTableConfig> getTableDetails() {
    return xClusterConfig.getTableDetails();
  }

  @ApiModelProperty(value = "List of table ids in replication")
  public Set<String> getTables() {
    return xClusterConfig.getTableIds();
  }

  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. List of db ids in replication")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  public Set<String> getDbs() {
    return xClusterConfig.getDbIds();
  }

  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. List of db details in replication")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  public Set<XClusterNamespaceConfig> getDbDetails() {
    return xClusterConfig.getNamespaceDetails();
  }

  @ApiModelProperty(
      value = "UUID of the underlying xCluster config that is managing the replication")
  public UUID getXClusterConfigUuid() {
    return xClusterConfig.getUuid();
  }

  @ApiModelProperty(
      value = "YbaApi Internal. The list of xCluster configs' uuids that belong to this dr config")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.1.0")
  public List<UUID> getXClusterConfigsUuid() {
    return drConfig.getXClusterConfigs().stream()
        .map(XClusterConfig::getUuid)
        .collect(Collectors.toList());
  }
}
