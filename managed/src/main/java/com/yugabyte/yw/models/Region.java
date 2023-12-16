// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import static io.ebean.DB.beginTransaction;
import static io.ebean.DB.commitTransaction;
import static io.ebean.DB.endTransaction;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonManagedReference;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.base.Strings;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.ProviderAndRegion;
import com.yugabyte.yw.models.helpers.provider.region.AWSRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import io.ebean.DB;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Junction;
import io.ebean.Model;
import io.ebean.Query;
import io.ebean.RawSql;
import io.ebean.RawSqlBuilder;
import io.ebean.SqlUpdate;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import io.ebean.annotation.Where;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.CascadeType;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.OneToMany;
import jakarta.persistence.Transient;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Getter;
import lombok.Setter;
import org.apache.commons.collections.CollectionUtils;
import play.data.validation.Constraints;
import play.libs.Json;

@Entity
@JsonInclude(JsonInclude.Include.NON_NULL)
@ApiModel(
    description =
        "Region within a given provider. Typically, this maps to a "
            + "single cloud provider region.")
@Getter
@Setter
public class Region extends Model {

  @Id
  @ApiModelProperty(value = "Region UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @Column(length = 25, nullable = false)
  @ApiModelProperty(
      value = "Cloud provider region code",
      example = "us-west-2",
      accessMode = READ_WRITE)
  private String code;

  @Column(length = 100, nullable = false)
  @ApiModelProperty(
      value = "Cloud provider region name",
      example = "US West (Oregon)",
      accessMode = READ_ONLY)
  private String name;

  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.17.2.0")
  @ApiModelProperty(
      value =
          "Deprecated since YBA version 2.17.2.0, "
              + "Moved to details.cloudInfo aws/gcp/azure ybImage property",
      example = "TODO",
      accessMode = READ_WRITE)
  private String ybImage;

  @Column(columnDefinition = "float")
  @ApiModelProperty(value = "The region's longitude", example = "-120.01", accessMode = READ_ONLY)
  @Constraints.Min(-180)
  @Constraints.Max(180)
  private double longitude = 0.0;

  @Column(columnDefinition = "float")
  @ApiModelProperty(value = "The region's latitude", example = "37.22", accessMode = READ_ONLY)
  @Constraints.Min(-90)
  @Constraints.Max(90)
  private double latitude = 0.0;

  @Column(nullable = false)
  @ManyToOne
  @JsonBackReference("provider-regions")
  private Provider provider;

  @OneToMany(cascade = CascadeType.ALL)
  @Where(clause = "t0.active = true")
  @JsonManagedReference("region-zones")
  private List<AvailabilityZone> zones;

  @ApiModelProperty(accessMode = READ_ONLY)
  @Column(nullable = false, columnDefinition = "boolean default true")
  private Boolean active = true;

  public boolean isActive() {
    return getActive() == null || getActive();
  }

  @Transient
  @ApiModelProperty(hidden = true)
  private String providerCode;

  @Encrypted
  @DbJson
  @Column(columnDefinition = "TEXT")
  @ApiModelProperty
  private RegionDetails details = new RegionDetails();

  @JsonIgnore
  public long getNodeCount() {
    Set<UUID> azUUIDs = getZones().stream().map(az -> az.getUuid()).collect(Collectors.toSet());
    return Customer.get(getProvider().getCustomerUUID())
        .getUniversesForProvider(getProvider().getUuid())
        .stream()
        .flatMap(u -> u.getUniverseDetails().nodeDetailsSet.stream())
        .filter(nd -> azUUIDs.contains(nd.azUuid))
        .count();
  }

  @JsonProperty("securityGroupId")
  public void setSecurityGroupId(String securityGroupId) {
    CloudType cloudType = this.getProviderCloudCode();
    if (cloudType == CloudType.aws) {
      AWSRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(this);
      regionCloudInfo.setSecurityGroupId(securityGroupId);
    } else if (cloudType == CloudType.azu) {
      AzureRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(this);
      regionCloudInfo.setSecurityGroupId(securityGroupId);
    }
  }

  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.17.2.0")
  @ApiModelProperty(
      required = false,
      value =
          "Deprecated since YBA version 2.17.2.0, "
              + "Moved to regionDetails.cloudInfo aws/azure securityGroupId property")
  public String getSecurityGroupId() {
    Map<String, String> envVars = CloudInfoInterface.fetchEnvVars(this);
    String sgNode = "";
    if (envVars.containsKey("securityGroupId")) {
      sgNode = envVars.getOrDefault("securityGroupId", null);
    }
    return sgNode == null || sgNode.isEmpty() ? null : sgNode;
  }

  @JsonProperty("vnetName")
  public void setVnetName(String vnetName) {
    CloudType cloudType = this.getProviderCloudCode();
    if (cloudType.equals(CloudType.aws)) {
      AWSRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(this);
      regionCloudInfo.setVnet(vnetName);
    } else if (cloudType.equals(CloudType.azu)) {
      AzureRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(this);
      regionCloudInfo.setVnet(vnetName);
    }
  }

  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.17.2.0")
  @ApiModelProperty(
      required = false,
      value =
          "Deprecated since YBA version 2.17.2.0, "
              + "Moved to regionDetails.cloudInfo aws/azure vnet property")
  public String getVnetName() {
    Map<String, String> envVars = CloudInfoInterface.fetchEnvVars(this);
    String vnetNode = "";
    if (envVars.containsKey("vnet")) {
      vnetNode = envVars.getOrDefault("vnet", null);
    }
    return vnetNode == null || vnetNode.isEmpty() ? null : vnetNode;
  }

  public void setArchitecture(Architecture arch) {
    CloudType cloudType = this.getProviderCloudCode();
    if (cloudType.equals(CloudType.aws)) {
      AWSRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(this);
      regionCloudInfo.setArch(arch);
    }
  }

  @JsonIgnore
  public Architecture getArchitecture() {
    CloudType cloudType = this.getProviderCloudCode();
    if (cloudType.equals(CloudType.aws)) {
      AWSRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(this);
      return regionCloudInfo.getArch();
    }
    return null;
  }

  @JsonIgnore
  @Deprecated
  public String getYbImageDeprecated() {
    return this.ybImage;
  }

  public String getYbImage() {
    Map<String, String> envVars = CloudInfoInterface.fetchEnvVars(this);
    if (envVars.containsKey("ybImage")) {
      return envVars.getOrDefault("ybImage", null);
    }
    return null;
  }

  public void setYbImage(String ybImage) {
    CloudType cloudType = this.getProviderCloudCode();
    if (cloudType.equals(CloudType.aws)) {
      AWSRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(this);
      regionCloudInfo.setYbImage(ybImage);
    } else if (cloudType.equals(CloudType.gcp)) {
      GCPRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(this);
      regionCloudInfo.setYbImage(ybImage);
    } else if (cloudType.equals(CloudType.azu)) {
      AzureRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(this);
      regionCloudInfo.setYbImage(ybImage);
    }
  }

  @Deprecated
  @DbJson
  @Column(columnDefinition = "TEXT")
  private Map<String, String> config;

  @Deprecated
  @JsonProperty("config")
  public void setConfig(Map<String, String> configMap) {
    if (configMap != null && !configMap.isEmpty()) {
      CloudInfoInterface.setCloudProviderInfoFromConfig(this, configMap);
    }
  }

  @JsonProperty("details")
  public RegionDetails getMaskRegionDetails() {
    return CloudInfoInterface.maskRegionDetails(this);
  }

  public RegionDetails getDetails() {
    if (details == null) {
      setDetails(new RegionDetails());
    }
    return details;
  }

  @JsonIgnore
  public boolean isUpdateNeeded(Region region) {
    boolean isUpdatedNeeded =
        !Objects.equals(this.getSecurityGroupId(), region.getSecurityGroupId())
            || !Objects.equals(this.getVnetName(), region.getVnetName())
            || !Objects.equals(this.getYbImage(), region.getYbImage())
            || !Objects.equals(this.getDetails(), region.getDetails());
    if (region.getProviderCloudCode() == CloudType.onprem) {
      isUpdatedNeeded |=
          !Objects.equals(this.getLatitude(), region.getLatitude())
              || !Objects.equals(this.getLongitude(), region.getLongitude());
    }
    return isUpdatedNeeded;
  }

  /** Query Helper for PlacementRegion with region code */
  public static final Finder<UUID, Region> find = new Finder<UUID, Region>(Region.class) {};

  /**
   * Create new instance of PlacementRegion
   *
   * @param provider Cloud Provider
   * @param code Unique PlacementRegion Code
   * @param name User Friendly PlacementRegion Name
   * @param ybImage The YB image ID that we need to use for provisioning in this region
   * @return instance of PlacementRegion
   */
  public static Region create(Provider provider, String code, String name, String ybImage) {
    return create(provider, code, name, ybImage, 0.0, 0.0);
  }

  // Overload create function with lat, long values for OnPrem case
  public static Region create(
      Provider provider,
      String code,
      String name,
      String ybImage,
      double latitude,
      double longitude) {
    return create(provider, code, name, ybImage, latitude, longitude, new RegionDetails());
  }

  public static Region create(
      Provider provider,
      String code,
      String name,
      String ybImage,
      double latitude,
      double longitude,
      RegionDetails details) {
    Region region = new Region();
    region.setProvider(provider);
    region.setCode(code);
    region.setName(name);
    region.setLatitude(latitude);
    region.setLongitude(longitude);
    region.setDetails(details);
    region.setYbImage(ybImage);
    region.save();
    return region;
  }

  public static Region create(
      Provider provider,
      String code,
      String name,
      String ybImage,
      double latitude,
      double longitude,
      Map<String, String> config) {
    Region region = new Region();
    region.setProvider(provider);
    region.setCode(code);
    region.setName(name);
    region.setYbImage(ybImage);
    region.setLatitude(latitude);
    region.setLongitude(longitude);
    region.setConfig(config);
    region.save();
    return region;
  }

  public static Region createWithMetadata(Provider provider, String code, JsonNode metadata) {
    Region region = Json.fromJson(metadata, Region.class);
    region.setProvider(provider);
    region.setCode(code);
    region.setDetails(new RegionDetails());
    if (metadata.has("ybImage")) {
      region.setYbImage(metadata.get("ybImage").textValue());
    }
    if (metadata.has("architecture")) {
      region.setArchitecture(Architecture.valueOf(metadata.get("architecture").textValue()));
    }
    region.save();
    return region;
  }

  /** DEPRECATED: use {@link #getOrBadRequest(UUID, UUID, UUID)} */
  @Deprecated()
  public static Region get(UUID regionUUID) {
    return find.query().fetch("provider").where().idEq(regionUUID).findOne();
  }

  public static List<Region> findByUuids(Collection<UUID> uuids) {
    return Region.find.query().where().idIn(uuids).findList();
  }

  public static Region getByCode(Provider provider, String code) {
    return find.query().where().eq("provider_UUID", provider.getUuid()).eq("code", code).findOne();
  }

  public static Optional<Region> maybeGetByCode(Provider provider, String code) {
    return find.query()
        .where()
        .eq("provider_UUID", provider.getUuid())
        .eq("code", code)
        .findOneOrEmpty();
  }

  public static List<Region> getByProvider(UUID providerUUID) {
    return getByProvider(providerUUID, true);
  }

  public static List<Region> getByProvider(UUID providerUUID, boolean onlyActive) {
    ExpressionList<Region> expr = find.query().where().eq("provider_UUID", providerUUID);
    if (onlyActive) {
      expr.eq("active", true);
    }
    return expr.findList();
  }

  public static List<Region> getFullByProviders(Collection<UUID> providers) {
    if (CollectionUtils.isEmpty(providers)) {
      return Collections.emptyList();
    }
    return find.query()
        .fetch("provider")
        .fetch("zones")
        .where()
        .in("provider_UUID", providers)
        .findList();
  }

  public static Region getOrBadRequest(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    Region region = get(customerUUID, providerUUID, regionUUID);
    if (region == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Provider/Region UUID");
    }
    return region;
  }

  public static Region getOrBadRequest(UUID regionUUID) {
    Region region = get(regionUUID);
    if (region == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Region UUID");
    }
    return region;
  }

  public static List<Region> findByKeys(Collection<ProviderAndRegion> keys) {
    if (CollectionUtils.isEmpty(keys)) {
      return Collections.emptyList();
    }
    Set<ProviderAndRegion> uniqueKeys = new HashSet<>(keys);
    ExpressionList<Region> query = find.query().where();
    Junction<Region> orExpr = query.or();
    for (ProviderAndRegion key : uniqueKeys) {
      Junction<Region> andExpr = orExpr.and();
      andExpr.eq("provider_UUID", key.getProviderUuid());
      andExpr.eq("code", key.getRegionCode());
      orExpr.endAnd();
    }
    return query.endOr().findList();
  }

  /** DEPRECATED: use {@link #getOrBadRequest(UUID, UUID, UUID)} */
  @Deprecated
  public static Region get(UUID customerUUID, UUID providerUUID, UUID regionUUID) {
    String regionQuery =
        " select r.uuid, r.code, r.name"
            + "   from region r join provider p on p.uuid = r.provider_uuid "
            + "  where r.uuid = :r_UUID and p.uuid = :p_UUID and p.customer_uuid = :c_UUID";

    RawSql rawSql = RawSqlBuilder.parse(regionQuery).create();
    Query<Region> query = DB.find(Region.class);
    query.setRawSql(rawSql);
    query.setParameter("r_UUID", regionUUID);
    query.setParameter("p_UUID", providerUUID);
    query.setParameter("c_UUID", customerUUID);
    return query.findOne();
  }

  /**
   * Fetch Regions with the minimum zone count and having a valid yb server image.
   *
   * @return List of PlacementRegion
   */
  public static List<Region> fetchValidRegions(
      UUID customerUUID, UUID providerUUID, int minZoneCount) {
    return fetchValidRegions(customerUUID, Collections.singletonList(providerUUID), minZoneCount);
  }

  /**
   * Fetch Regions with the minimum zone count and having a valid yb server image.
   *
   * @return List of PlacementRegion
   */
  public static List<Region> fetchValidRegions(
      UUID customerUUID, Collection<UUID> providerUUIDs, int minZoneCount) {
    if (CollectionUtils.isEmpty(providerUUIDs)) {
      return Collections.emptyList();
    }
    String regionQuery =
        " select r.uuid, r.code, r.name, r.provider_uuid"
            + "   from region r join provider p on p.uuid = r.provider_uuid "
            + "   left outer join availability_zone zone "
            + " on zone.region_uuid = r.uuid and zone.active = true "
            + "  where p.uuid in (:p_UUIDs) and p.customer_uuid = :c_UUID and r.active = true"
            + "  group by r.uuid "
            + " having count(zone.uuid) >= "
            + minZoneCount;

    RawSql rawSql =
        RawSqlBuilder.parse(regionQuery).columnMapping("r.provider_uuid", "provider.uuid").create();
    Query<Region> query = DB.find(Region.class);
    query.setRawSql(rawSql);
    query.setParameter("p_UUIDs", providerUUIDs);
    query.setParameter("c_UUID", customerUUID);
    return query.findList();
  }

  @JsonIgnore
  public CloudType getProviderCloudCode() {
    if (provider != null) {
      return provider.getCloudCode();
    } else if (!Strings.isNullOrEmpty(providerCode)) {
      return CloudType.valueOf(providerCode);
    }

    return CloudType.other;
  }

  public void disableRegionAndZones() {
    beginTransaction();
    try {
      setActive(false);
      update();
      String s =
          "UPDATE availability_zone set active = :active_flag where region_uuid = :region_UUID";
      SqlUpdate updateStmt = DB.sqlUpdate(s);
      updateStmt.setParameter("active_flag", false);
      updateStmt.setParameter("region_UUID", getUuid());
      DB.getDefault().execute(updateStmt);
      commitTransaction();
    } catch (Exception e) {
      throw new RuntimeException("Unable to flag Region UUID as deleted: " + getUuid());
    } finally {
      endTransaction();
    }
  }

  /**
   * Returns a complete list of AZ's for region (including inactive)
   *
   * @return list of zones
   */
  @JsonIgnore
  public List<AvailabilityZone> getAllZones() {
    return AvailabilityZone.getAZsForRegion(this.getUuid(), false);
  }

  public String toString() {
    return Json.newObject()
        .put("code", getCode())
        .put("provider", getProvider().getUuid().toString())
        .put("name", getName())
        .put("ybImage", getYbImage())
        .put("latitude", getLatitude())
        .put("longitude", getLongitude())
        .toString();
  }
}
