// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.YWServiceException;
import io.ebean.*;
import io.ebean.annotation.DbJson;
import play.data.validation.Constraints;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.ManyToOne;
import java.util.*;

import static com.yugabyte.yw.models.helpers.CommonUtils.maskConfig;
import static play.mvc.Http.Status.BAD_REQUEST;

@Entity
public class AvailabilityZone extends Model {

  @Id
  public UUID uuid;

  @Column(length = 25, nullable = false)
  public String code;

  @Column(length = 100, nullable = false)
  @Constraints.Required
  public String name;

  @Constraints.Required
  @Column(nullable = false)
  @ManyToOne
  @JsonBackReference
  public Region region;

  @Column(nullable = false, columnDefinition = "boolean default true")
  public Boolean active = true;

  public Boolean isActive() {
    return active;
  }

  public void setActiveFlag(Boolean active) {
    this.active = active;
  }

  @Column(length = 50)
  public String subnet;

  @DbJson
  @Column(columnDefinition = "TEXT")
  public JsonNode config;

  public String getKubeconfigPath() {
    Map<String, String> configMap = this.getConfig();
    return configMap.getOrDefault("KUBECONFIG", null);
  }

  public void setConfig(Map<String, String> configMap) {
    Map<String, String> currConfig = this.getConfig();
    for (String key : configMap.keySet()) {
      currConfig.put(key, configMap.get(key));
    }
    this.config = Json.toJson(currConfig);
    this.save();
  }

  @JsonIgnore
  public JsonNode getMaskedConfig() {
    if (this.config == null) {
      return Json.newObject();
    } else {
      return maskConfig(this.config);
    }
  }

  @JsonIgnore
  public Map<String, String> getConfig() {
    if (this.config == null) {
      return new HashMap();
    } else {
      return Json.fromJson(this.config, Map.class);
    }
  }

  /**
   * Query Helper for Availability Zone with primary key
   */
  public static final Finder<UUID, AvailabilityZone> find =
    new Finder<UUID, AvailabilityZone>(AvailabilityZone.class) {
    };

  public static AvailabilityZone create(Region region, String code, String name, String subnet) {
    AvailabilityZone az = new AvailabilityZone();
    az.region = region;
    az.code = code;
    az.name = name;
    az.subnet = subnet;
    az.save();
    return az;
  }

  public static List<AvailabilityZone> getAZsForRegion(UUID regionUUID) {
    return find.query().where().eq("region_uuid", regionUUID).findList();
  }

  public static Set<AvailabilityZone> getAllByCode(String code) {
    return find.query().where().eq("code", code).findSet();
  }

  public static AvailabilityZone getByCode(Provider provider, String code) {
    return maybeGetByCode(provider, code)
      .orElseThrow(() -> new RuntimeException(
        "AZ by code: " + code + " and provider " + provider.code + " NOT FOUND "));
  }

  public static Optional<AvailabilityZone> maybeGetByCode(Provider provider, String code) {
    return getAllByCode(code).stream()
      .filter(az -> az.getProvider().uuid.equals(provider.uuid))
      .findFirst();
  }

  public static AvailabilityZone getOrBadRequest(UUID zoneUuid) {
    return maybeGet(zoneUuid)
      .orElseThrow(() -> new YWServiceException(
        BAD_REQUEST, "Invalid AvailabilityZone UUID: " + zoneUuid));
  }

  // TODO getOrNull should be replaced by maybeGet or getOrBadRequest
  @Deprecated
  public static AvailabilityZone get(UUID zoneUuid) {
    return maybeGet(zoneUuid).orElse(null);
  }

  public static Optional<AvailabilityZone> maybeGet(UUID zoneUuid) {
    return AvailabilityZone.find.query().
      fetch("region")
      .fetch("region.provider")
      .where()
      .idEq(zoneUuid)
      .findOneOrEmpty();
  }

  @JsonBackReference
  public Provider getProvider() {
    String providerQuery =
      "select p.uuid, p.code, p.name from provider p where p.uuid = :p_uuid";
    RawSql rawSql = RawSqlBuilder.parse(providerQuery).create();
    Query<Provider> query = Ebean.find(Provider.class);
    query.setRawSql(rawSql);
    query.setParameter("p_uuid", region.provider.uuid);
    return query.findOne();
  }

  @Override
  public String toString() {
    return "AvailabilityZone{" +
      "uuid=" + uuid +
      ", code='" + code + '\'' +
      ", name='" + name + '\'' +
      ", region=" + region +
      ", active=" + active +
      ", subnet='" + subnet + '\'' +
      ", config=" + config +
      '}';
  }
}
