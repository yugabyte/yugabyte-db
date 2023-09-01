package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import io.ebean.DB;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.SqlRow;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.JoinColumn;
import javax.persistence.ManyToOne;
import javax.persistence.Transient;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.TableType;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@ApiModel(description = "PITR config created on the universe")
@Entity
@Data
@Slf4j
@EqualsAndHashCode(callSuper = false)
public class PitrConfig extends Model {

  private static final Finder<UUID, PitrConfig> find = new Finder<>(PitrConfig.class) {};

  @Id
  @ApiModelProperty(value = "PITR config UUID")
  private UUID uuid;

  @Column
  @ApiModelProperty(value = "PITR config name")
  private String name;

  @ApiModelProperty(value = "Customer UUID of this config", accessMode = READ_WRITE)
  @Column(nullable = false)
  private UUID customerUUID;

  @ApiModelProperty(value = "Universe UUID of this config", accessMode = READ_WRITE)
  @ManyToOne
  @JoinColumn(name = "universe_uuid", referencedColumnName = "universe_uuid")
  @JsonBackReference
  private Universe universe;

  @Transient private State state;

  @Transient private long minRecoverTimeInMillis;

  @Transient private long maxRecoverTimeInMillis;

  @ApiModelProperty(value = "Table Type", accessMode = READ_WRITE)
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private TableType tableType;

  @ApiModelProperty(value = "DB Name", accessMode = READ_WRITE)
  @Column(nullable = false)
  private String dbName;

  @ApiModelProperty(value = "Interval between snasphots in seconds", accessMode = READ_WRITE)
  @Column(nullable = false)
  private long scheduleInterval = 86400L;

  @ApiModelProperty(value = "Retention Period in seconds", accessMode = READ_WRITE)
  @Column(nullable = false)
  private long retentionPeriod = 86400L * 7L;

  @ApiModelProperty(
      value = "Create time of the PITR config",
      accessMode = READ_ONLY,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @Column
  private Date createTime;

  @ApiModelProperty(
      value = "Update time of the PITR con",
      accessMode = READ_WRITE,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @Column
  private Date updateTime;

  public static PitrConfig create(UUID scheduleUUID, CreatePitrConfigParams params) {
    PitrConfig pitrConfig = new PitrConfig();
    pitrConfig.setUuid(scheduleUUID);
    pitrConfig.setName(params.name);
    pitrConfig.setCustomerUUID(params.customerUUID);
    pitrConfig.setUniverse(Universe.getOrBadRequest(params.getUniverseUUID()));
    pitrConfig.setTableType(params.tableType);
    pitrConfig.setDbName(params.keyspaceName);
    pitrConfig.setScheduleInterval(params.intervalInSeconds);
    pitrConfig.setRetentionPeriod(params.retentionPeriodInSeconds);
    Date currentDate = new Date();
    pitrConfig.setCreateTime(currentDate);
    pitrConfig.setUpdateTime(currentDate);
    pitrConfig.save();
    return pitrConfig;
  }

  public static List<PitrConfig> getAll() {
    return find.query().findList();
  }

  public static List<PitrConfig> getByUniverseUUID(UUID universeUUID) {
    return find.query().where().eq("universe_uuid", universeUUID).findList();
  }

  public static PitrConfig get(UUID configUUID) {
    return find.query().where().eq("uuid", configUUID).findOne();
  }

  public static PitrConfig getOrBadRequest(UUID configUUID) {
    return maybeGet(configUUID)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST, "Cannot find snapshot schedule " + configUUID));
  }

  public static Optional<PitrConfig> maybeGet(UUID configUUID) {
    return find.query().where().eq("uuid", configUUID).findOneOrEmpty();
  }

  public static Optional<PitrConfig> maybeGet(
      UUID universeUUID, TableType tableType, String dbName) {
    return find.query()
        .where()
        .eq("universe_uuid", universeUUID)
        .eq("table_type", tableType.toString())
        .eq("db_name", dbName)
        .findOneOrEmpty();
  }

  @JsonProperty
  public boolean isUsedForXCluster() {
    String sqlStatement = "SELECT xcluster_uuid FROM xcluster_pitr WHERE pitr_uuid = :pitrUuid";
    List<SqlRow> sqlRow = DB.sqlQuery(sqlStatement).setParameter("pitrUuid", this.uuid).findList();
    return !sqlRow.isEmpty();
  }
}
