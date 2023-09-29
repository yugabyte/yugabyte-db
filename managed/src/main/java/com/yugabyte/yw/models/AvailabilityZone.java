// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.base.Strings;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.ManyToOne;
import javax.persistence.Transient;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@Entity
@ApiModel(description = "Availability zone (AZ) for a region")
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class AvailabilityZone extends Model {

  @Id
  @ApiModelProperty(value = "AZ UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @Column(length = 25, nullable = false)
  @ApiModelProperty(value = "AZ code", example = "us-west1-a")
  private String code;

  @Column(length = 100, nullable = false)
  @Constraints.Required
  @ApiModelProperty(value = "AZ name", example = "us-west1-a", required = true)
  private String name;

  @Constraints.Required
  @Column(nullable = false)
  @ManyToOne
  @JsonBackReference("region-zones")
  @ApiModelProperty(value = "AZ region", example = "South east 1", required = true)
  private Region region;

  @Column(nullable = false, columnDefinition = "boolean default true")
  @ApiModelProperty(
      value = "AZ status. This value is `true` for an active AZ.",
      accessMode = READ_ONLY)
  private Boolean active = true;

  public boolean isActive() {
    return getActive();
  }

  @Column(length = 80)
  @ApiModelProperty(value = "AZ subnet", example = "subnet id")
  private String subnet;

  @Column(length = 80)
  @ApiModelProperty(value = "AZ secondary subnet", example = "secondary subnet id")
  private String secondarySubnet;

  @Transient
  @ApiModelProperty(hidden = true)
  private String providerCode;

  @Deprecated
  @DbJson
  @Column(columnDefinition = "TEXT")
  @ApiModelProperty(value = "AZ configuration values")
  private Map<String, String> config;

  @Encrypted
  @DbJson
  @Column(columnDefinition = "TEXT")
  @ApiModelProperty
  private AvailabilityZoneDetails details = new AvailabilityZoneDetails();

  @ApiModelProperty(value = "Path to Kubernetes configuration file", accessMode = READ_ONLY)
  public String getKubeconfigPath() {
    Map<String, String> configMap = CloudInfoInterface.fetchEnvVars(this);
    return configMap.getOrDefault("KUBECONFIG", null);
  }

  @Deprecated
  @JsonProperty("config")
  public void setConfig(Map<String, String> configMap) {
    if (configMap != null && !configMap.isEmpty()) {
      CloudInfoInterface.setCloudProviderInfoFromConfig(this, configMap);
    }
  }

  @Deprecated
  public void updateConfig(Map<String, String> configMap) {
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(this);
    config.putAll(configMap);
    setConfig(config);
  }

  @JsonProperty("details")
  public void setAvailabilityZoneDetails(AvailabilityZoneDetails azDetails) {
    this.setDetails(azDetails);
  }

  @JsonProperty("details")
  public AvailabilityZoneDetails getMaskAvailabilityZoneDetails() {
    return CloudInfoInterface.maskAvailabilityZoneDetails(this);
  }

  @JsonIgnore
  public AvailabilityZoneDetails getAvailabilityZoneDetails() {
    if (getDetails() == null) {
      setDetails(new AvailabilityZoneDetails());
    }
    return getDetails();
  }

  @JsonIgnore
  public boolean isUpdateNeeded(AvailabilityZone zone) {
    return !Objects.equals(this.getSubnet(), zone.getSubnet())
        || !Objects.equals(this.getSecondarySubnet(), zone.getSecondarySubnet())
        || !Objects.equals(this.getDetails(), zone.getDetails())
        || !Objects.equals(
            CloudInfoInterface.fetchEnvVars(this), CloudInfoInterface.fetchEnvVars(zone));
  }

  /** Query Helper for Availability Zone with primary key */
  public static final Finder<UUID, AvailabilityZone> find =
      new Finder<UUID, AvailabilityZone>(AvailabilityZone.class) {};

  public static final Logger LOG = LoggerFactory.getLogger(AvailabilityZone.class);

  @JsonIgnore
  public long getNodeCount() {
    return Customer.get(getRegion().getProvider().getCustomerUUID())
        .getUniversesForProvider(getRegion().getProvider().getUuid())
        .stream()
        .flatMap(u -> u.getUniverseDetails().nodeDetailsSet.stream())
        .filter(nd -> nd.azUuid.equals(getUuid()))
        .count();
  }

  public static AvailabilityZone createOrThrow(
      Region region, String code, String name, String subnet) {
    return createOrThrow(region, code, name, subnet, null);
  }

  public static AvailabilityZone createOrThrow(
      Region region, String code, String name, String subnet, String secondarySubnet) {
    return createOrThrow(
        region, code, name, subnet, secondarySubnet, new AvailabilityZoneDetails());
  }

  public static AvailabilityZone createOrThrow(
      Region region,
      String code,
      String name,
      String subnet,
      String secondarySubnet,
      AvailabilityZoneDetails details) {
    try {
      AvailabilityZone az = new AvailabilityZone();
      az.setRegion(region);
      az.setCode(code);
      az.setName(name);
      az.setSubnet(subnet);
      az.setAvailabilityZoneDetails(details);
      az.setSecondarySubnet(secondarySubnet);
      az.save();
      return az;
    } catch (Exception e) {
      LOG.error(e.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Unable to create zone: " + code);
    }
  }

  @Deprecated
  public static AvailabilityZone createOrThrow(
      Region region,
      String code,
      String name,
      String subnet,
      String secondarySubnet,
      Map<String, String> config) {
    try {
      AvailabilityZone az = new AvailabilityZone();
      az.setRegion(region);
      az.setCode(code);
      az.setName(name);
      az.setSubnet(subnet);
      az.setConfig(config);
      az.setSecondarySubnet(secondarySubnet);
      az.save();
      return az;
    } catch (Exception e) {
      LOG.error(e.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Unable to create zone: " + code);
    }
  }

  public static List<AvailabilityZone> getAZsForRegion(UUID regionUUID) {
    return getAZsForRegion(regionUUID, true);
  }

  public static List<AvailabilityZone> getAZsForRegion(UUID regionUUID, boolean onlyActive) {
    ExpressionList<AvailabilityZone> expr = find.query().where().eq("region_uuid", regionUUID);
    if (onlyActive) {
      expr.eq("active", true);
    }
    return expr.findList();
  }

  public static AvailabilityZone getByRegionOrBadRequest(UUID azUUID, UUID regionUUID) {
    AvailabilityZone availabilityZone =
        AvailabilityZone.find.query().where().idEq(azUUID).eq("region_uuid", regionUUID).findOne();
    if (availabilityZone == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Region/AZ UUID:" + azUUID);
    }
    return availabilityZone;
  }

  public static AvailabilityZone getByCode(Provider provider, String code) {
    return getByCode(provider, code, true);
  }

  public static AvailabilityZone getByCode(Provider provider, String code, boolean onlyActive) {
    return maybeGetByCode(provider, code, onlyActive)
        .orElseThrow(
            () ->
                new RuntimeException(
                    "AZ by code " + code + " and provider " + provider.getCode() + " NOT FOUND "));
  }

  public static Optional<AvailabilityZone> maybeGetByCode(Provider provider, String code) {
    return maybeGetByCode(provider, code, true);
  }

  public static Optional<AvailabilityZone> maybeGetByCode(
      Provider provider, String code, boolean onlyActive) {
    return find.query().where().eq("code", code).findSet().stream()
        .filter(az -> az.getProvider().getUuid().equals(provider.getUuid()))
        .filter(az -> !onlyActive || az.getActive())
        .findFirst();
  }

  @JsonIgnore
  public CloudType getProviderCloudCode() {
    if (region != null) {
      return region.getProviderCloudCode();
    } else if (!Strings.isNullOrEmpty(providerCode)) {
      return CloudType.valueOf(providerCode);
    }

    return CloudType.other;
  }

  public static AvailabilityZone getOrBadRequest(UUID zoneUuid) {
    return maybeGet(zoneUuid)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST, "Invalid AvailabilityZone UUID: " + zoneUuid));
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
    return Provider.find
        .query()
        .fetchLazy("regions")
        .where()
        .eq("uuid", getRegion().getProvider().getUuid())
        .findOne();
  }

  @Override
  public String toString() {
    return "AvailabilityZone{"
        + "uuid="
        + getUuid()
        + ", code='"
        + getCode()
        + '\''
        + ", name='"
        + getName()
        + '\''
        + ", region="
        + getRegion()
        + ", active="
        + getActive()
        + ", subnet='"
        + getSubnet()
        + '\''
        + ", config="
        + getConfig()
        + '}';
  }
}
