// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbEnumValue;
import io.ebean.annotation.Transactional;
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
import lombok.extern.slf4j.Slf4j;

@Slf4j
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
  @ApiModelProperty(value = "Status", allowableValues = "Init, Running, Updating, Paused, Failed")
  public XClusterConfigStatusType status;

  public enum XClusterConfigStatusType {
    Init("Init"),
    Running("Running"),
    Updating("Updating"),
    Paused("Paused"),
    Failed("Failed");

    private String status;

    XClusterConfigStatusType(String status) {
      this.status = status;
    }

    @Override
    @DbEnumValue
    public String toString() {
      return this.status;
    }
  }

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
    return this.tables.stream().map(table -> table.getTableID()).collect(Collectors.toSet());
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

  @JsonIgnore
  public String getReplicationGroupName() {
    return this.sourceUniverseUUID + "_" + this.name;
  }

  @Transactional
  public static XClusterConfig create(
      XClusterConfigCreateFormData formData, XClusterConfigStatusType status) {
    XClusterConfig xClusterConfig = new XClusterConfig();
    xClusterConfig.uuid = UUID.randomUUID();
    xClusterConfig.name = formData.name;
    xClusterConfig.sourceUniverseUUID = formData.sourceUniverseUUID;
    xClusterConfig.targetUniverseUUID = formData.targetUniverseUUID;
    xClusterConfig.status = status;
    xClusterConfig.createTime = new Date();
    xClusterConfig.modifyTime = new Date();
    xClusterConfig.setTables(formData.tables);
    xClusterConfig.save();
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
        find.query().fetch("tables", "tableID").where().eq("uuid", xClusterConfigUUID).findOne();
    if (xClusterConfig == null) {
      log.info("Cannot find XClusterConfig {}", xClusterConfigUUID);
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

  public static XClusterConfig getByNameSourceTarget(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    return find.query()
        .fetch("tables", "tableID")
        .where()
        .eq("config_name", name)
        .eq("source_universe_uuid", sourceUniverseUUID)
        .eq("target_universe_uuid", targetUniverseUUID)
        .findOne();
  }

  private static void checkXClusterConfigInCustomer(
      XClusterConfig xClusterConfig, Customer customer) {
    if (!customer.getUniverseUUIDs().contains(xClusterConfig.sourceUniverseUUID)
        || !customer.getUniverseUUIDs().contains(xClusterConfig.targetUniverseUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "XClusterConfig %s doesn't belong to Customer %s",
              xClusterConfig.uuid, customer.uuid));
    }
  }
}
