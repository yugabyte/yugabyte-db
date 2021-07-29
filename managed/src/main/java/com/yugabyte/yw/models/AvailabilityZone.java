// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.YWServiceException;
import io.ebean.Ebean;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.Query;
import io.ebean.RawSql;
import io.ebean.RawSqlBuilder;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.ManyToOne;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@Entity
@ApiModel(description = "Availability Zone of regions.")
public class AvailabilityZone extends Model {

  @Id
  @ApiModelProperty(value = "AZ uuid", accessMode = READ_ONLY)
  public UUID uuid;

  @Column(length = 25, nullable = false)
  @ApiModelProperty(value = "AZ code", example = "AWS")
  public String code;

  @Column(length = 100, nullable = false)
  @Constraints.Required
  @ApiModelProperty(value = "AZ name", example = "south-east-1", required = true)
  public String name;

  @Constraints.Required
  @Column(nullable = false)
  @ManyToOne
  @JsonBackReference("region-zones")
  @ApiModelProperty(value = "Region of AZ", example = "South east 1", required = true)
  public Region region;

  @Column(nullable = false, columnDefinition = "boolean default true")
  @ApiModelProperty(value = "AZ is active or not", accessMode = READ_ONLY)
  public Boolean active = true;

  public Boolean isActive() {
    return active;
  }

  public void setActiveFlag(Boolean active) {
    this.active = active;
  }

  @Column(length = 50)
  @ApiModelProperty(value = "AZ Subnet", example = "subnet id")
  public String subnet;

  @DbJson
  @Column(columnDefinition = "TEXT")
  @ApiModelProperty(value = "AZ Config values")
  public Map<String, String> config;

  @ApiModelProperty(value = "Kubernetes Config path", accessMode = READ_ONLY)
  public String getKubeconfigPath() {
    Map<String, String> configMap = this.getConfig();
    return configMap.getOrDefault("KUBECONFIG", null);
  }

  public void setConfig(Map<String, String> configMap) {
    this.config = configMap;
    this.save();
  }

  public void updateConfig(Map<String, String> configMap) {
    Map<String, String> config = getConfig();
    config.putAll(configMap);
    setConfig(config);
  }

  @JsonIgnore
  public Map<String, String> getConfig() {
    return this.config == null ? new HashMap<>() : this.config;
  }

  /** Query Helper for Availability Zone with primary key */
  public static final Finder<UUID, AvailabilityZone> find =
      new Finder<UUID, AvailabilityZone>(AvailabilityZone.class) {};

  public static final Logger LOG = LoggerFactory.getLogger(AvailabilityZone.class);

  public static AvailabilityZone createOrThrow(
      Region region, String code, String name, String subnet) {
    try {
      AvailabilityZone az = new AvailabilityZone();
      az.region = region;
      az.code = code;
      az.name = name;
      az.subnet = subnet;
      az.save();
      return az;
    } catch (Exception e) {
      LOG.error(e.getMessage());
      throw new YWServiceException(INTERNAL_SERVER_ERROR, "Unable to create zone: " + code);
    }
  }

  public static List<AvailabilityZone> getAZsForRegion(UUID regionUUID) {
    return find.query().where().eq("region_uuid", regionUUID).findList();
  }

  public static Set<AvailabilityZone> getAllByCode(String code) {
    return find.query().where().eq("code", code).findSet();
  }

  public static AvailabilityZone getByRegionOrBadRequest(UUID azUUID, UUID regionUUID) {
    AvailabilityZone availabilityZone =
        AvailabilityZone.find.query().where().idEq(azUUID).eq("region_uuid", regionUUID).findOne();
    if (availabilityZone == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Region/AZ UUID:" + azUUID);
    }
    return availabilityZone;
  }

  public static AvailabilityZone getByCode(Provider provider, String code) {
    return maybeGetByCode(provider, code)
        .orElseThrow(
            () ->
                new RuntimeException(
                    "AZ by code: " + code + " and provider " + provider.code + " NOT FOUND "));
  }

  public static Optional<AvailabilityZone> maybeGetByCode(Provider provider, String code) {
    return getAllByCode(code)
        .stream()
        .filter(az -> az.getProvider().uuid.equals(provider.uuid))
        .findFirst();
  }

  public static AvailabilityZone getOrBadRequest(UUID zoneUuid) {
    return maybeGet(zoneUuid)
        .orElseThrow(
            () ->
                new YWServiceException(BAD_REQUEST, "Invalid AvailabilityZone UUID: " + zoneUuid));
  }

  // TODO getOrNull should be replaced by maybeGet or getOrBadRequest
  @Deprecated
  public static AvailabilityZone get(UUID zoneUuid) {
    return maybeGet(zoneUuid).orElse(null);
  }

  public static Optional<AvailabilityZone> maybeGet(UUID zoneUuid) {
    return AvailabilityZone.find
        .query()
        .fetch("region")
        .fetch("region.provider")
        .where()
        .idEq(zoneUuid)
        .findOneOrEmpty();
  }

  @JsonBackReference
  public Provider getProvider() {
    String providerQuery = "select p.uuid, p.code, p.name from provider p where p.uuid = :p_uuid";
    RawSql rawSql = RawSqlBuilder.parse(providerQuery).create();
    Query<Provider> query = Ebean.find(Provider.class);
    query.setRawSql(rawSql);
    query.setParameter("p_uuid", region.provider.uuid);
    return query.findOne();
  }

  @Override
  public String toString() {
    return "AvailabilityZone{"
        + "uuid="
        + uuid
        + ", code='"
        + code
        + '\''
        + ", name='"
        + name
        + '\''
        + ", region="
        + region
        + ", active="
        + active
        + ", subnet='"
        + subnet
        + '\''
        + ", config="
        + config
        + '}';
  }
}
