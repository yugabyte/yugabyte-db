package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.CascadeType;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.JoinColumn;
import javax.persistence.ManyToOne;
import javax.persistence.OneToMany;
import javax.persistence.Table;
import javax.persistence.UniqueConstraint;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.db.ebean.Transactional;

@Table(
    uniqueConstraints =
        @UniqueConstraint(
            columnNames = {
              "source_universe_uuid",
              "target_universe_uuid",
            }))
@Entity
@ApiModel(description = "xcluster config object")
public class XClusterConfig extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(XClusterConfig.class);

  private static final Finder<UUID, XClusterConfig> find =
      new Finder<UUID, XClusterConfig>(XClusterConfig.class) {};

  @Id
  @Column(name = "uuid")
  @ApiModelProperty(value = "UUID")
  public UUID uuid;

  @Column(name = "config_name")
  @ApiModelProperty(value = "Name")
  public String name;

  @ManyToOne
  @JoinColumn(name = "source_universe_uuid")
  @ApiModelProperty(value = "Source Universe UUID")
  public UUID sourceUniverseUUID;

  @ManyToOne
  @JoinColumn(name = "target_universe_uuid")
  @ApiModelProperty(value = "Target Universe UUID")
  public UUID targetUniverseUUID;

  @Column(name = "status")
  @ApiModelProperty(value = "Status", allowableValues = "Init, Running, Paused, Failed")
  public String status;

  @Column(name = "create_time")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(value = "Create time")
  public Date createTime;

  @Column(name = "modify_time")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(value = "Modify time")
  public Date modifyTime;

  @OneToMany(cascade = CascadeType.ALL)
  @JoinColumn(name = "config_uuid", referencedColumnName = "uuid")
  public Set<XClusterTableConfig> tables;

  @JsonProperty
  @ApiModelProperty(value = "Source Universe table IDs")
  public Set<String> getTables() {
    return this.tables.stream().map(table -> table.tableID).collect(Collectors.toSet());
  }

  @JsonProperty
  @ApiModelProperty(value = "Source Universe table IDs")
  public void setTables(Set<String> newTables) {
    this.tables = new HashSet<>();
    newTables.forEach(
        (id) -> {
          tables.add(new XClusterTableConfig(id));
        });
  }

  @Transactional
  public static XClusterConfig create(XClusterConfigCreateFormData formData) {
    XClusterConfig relationship = new XClusterConfig();
    relationship.uuid = UUID.randomUUID();
    relationship.name = formData.name;
    relationship.sourceUniverseUUID = formData.sourceUniverseUUID;
    relationship.targetUniverseUUID = formData.targetUniverseUUID;
    relationship.status = "Init";
    relationship.createTime = new Date();
    relationship.modifyTime = new Date();
    relationship.setTables(formData.tables);
    relationship.save();
    return relationship;
  }

  @Transactional
  public void updateFrom(XClusterConfigEditFormData formData) {
    this.name = formData.name;
    this.status = formData.status;
    this.modifyTime = new Date();
    this.setTables(formData.tables);
    this.update();
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
                    BAD_REQUEST, "Cannot find xcluster config " + xClusterConfigUUID));
  }

  public static Optional<XClusterConfig> maybeGet(UUID xClusterConfigUUID) {
    XClusterConfig xClusterConfig =
        find.query().fetch("tables", "tableID").where().eq("uuid", xClusterConfigUUID).findOne();
    if (xClusterConfig == null) {
      LOG.info("Cannot find xcluster config {}", xClusterConfigUUID);
      return Optional.empty();
    }
    return Optional.of(xClusterConfig);
  }

  public static List<XClusterConfig> getByTargetUniverseUUID(UUID targetUniverseUUID) {
    return find.query()
        .fetch("tables", "tableID")
        .where()
        .eq("target_universe_uuid", targetUniverseUUID)
        .findList();
  }

  public static List<XClusterConfig> getBySourceUniverseUUID(UUID sourceUniverseUUID) {
    return find.query()
        .fetch("tables", "tableID")
        .where()
        .eq("source_universe_uuid", sourceUniverseUUID)
        .findList();
  }

  public static List<XClusterConfig> getBetweenUniverses(
      UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    return find.query()
        .fetch("tables", "tableID")
        .where()
        .eq("source_universe_uuid", sourceUniverseUUID)
        .eq("target_universe_uuid", targetUniverseUUID)
        .findList();
  }

  private static void checkXClusterConfigInCustomer(
      XClusterConfig xClusterConfig, Customer customer) {
    if (!customer.getUniverseUUIDs().contains(xClusterConfig.sourceUniverseUUID)
        || !customer.getUniverseUUIDs().contains(xClusterConfig.targetUniverseUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "XCluster config UUID: %s doesn't belong " + "to Customer UUID: %s",
              xClusterConfig.uuid, customer.uuid));
    }
  }
}
