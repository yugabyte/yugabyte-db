// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.DEFAULT_YB_HOME_DIR;
import static com.yugabyte.yw.models.helpers.CommonUtils.maskConfigNew;
import static com.yugabyte.yw.models.helpers.CommonUtils.encryptProviderConfig;
import static com.yugabyte.yw.models.helpers.CommonUtils.decryptProviderConfig;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonManagedReference;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap.Params.PerRegionMetadata;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.persistence.CascadeType;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.OneToMany;
import javax.persistence.Table;
import javax.persistence.Transient;
import javax.persistence.UniqueConstraint;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@Table(uniqueConstraints = @UniqueConstraint(columnNames = {"customer_uuid", "name", "code"}))
@Entity
public class Provider extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Provider.class);
  private static final String TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST =
      "Transient property - only present in mutate API request";

  @ApiModelProperty(value = "Provider uuid", accessMode = READ_ONLY)
  @Id
  public UUID uuid;

  // TODO: Use Enum
  @Column(nullable = false)
  @ApiModelProperty(value = "Provider cloud code", accessMode = READ_WRITE)
  @Constraints.Required()
  public String code;

  @JsonIgnore
  public CloudType getCloudCode() {
    return CloudType.valueOf(this.code);
  }

  @Column(nullable = false)
  @ApiModelProperty(value = "Provider name", accessMode = READ_WRITE)
  @Constraints.Required()
  public String name;

  @Column(nullable = false, columnDefinition = "boolean default true")
  @ApiModelProperty(value = "Provider active status", accessMode = READ_ONLY)
  public Boolean active = true;

  @Column(name = "customer_uuid", nullable = false)
  @ApiModelProperty(value = "Customer uuid", accessMode = READ_ONLY)
  public UUID customerUUID;

  public static final Set<Common.CloudType> InstanceTagsEnabledProviders =
      ImmutableSet.of(Common.CloudType.aws, Common.CloudType.azu, Common.CloudType.gcp);
  public static final Set<Common.CloudType> InstanceTagsModificationEnabledProviders =
      ImmutableSet.of(Common.CloudType.aws, Common.CloudType.gcp);

  @JsonIgnore
  public void setCustomerUuid(UUID id) {
    this.customerUUID = id;
  }

  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  @Encrypted
  private Map<String, String> config;

  @OneToMany(cascade = CascadeType.ALL)
  @JsonManagedReference(value = "provider-regions")
  public List<Region> regions;

  @JsonIgnore
  @OneToMany(mappedBy = "provider", cascade = CascadeType.ALL)
  public Set<InstanceType> instanceTypes;

  @JsonIgnore
  @OneToMany(mappedBy = "provider", cascade = CascadeType.ALL)
  public Set<PriceComponent> priceComponents;

  // Start Transient Properties
  // TODO: These are all transient fields for now. At present these are stored
  //  with CloudBootstrap params. We should move them to Provider and persist with
  //  Provider entity.

  // Custom keypair name to use when spinning up YB nodes.
  // Default: created and managed by YB.
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public String keyPairName = null;

  // Custom SSH private key component.
  // Default: created and managed by YB.
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public String sshPrivateKeyContent = null;

  // Custom SSH user to login to machines.
  // Default: created and managed by YB.
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public String sshUser = null;

  // Whether provider should use airgapped install.
  // Default: false.
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public boolean airGapInstall = false;

  // Port to open for connections on the instance.
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public Integer sshPort = 22;

  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public String hostVpcId = null;

  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public String hostVpcRegion = null;

  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public List<String> customHostCidrs = new ArrayList<>();
  // TODO(bogdan): only used/needed for GCP.

  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public String destVpcId = null;

  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public boolean overrideKeyValidate = false;

  // Whether or not to set up NTP
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public boolean setUpChrony = false;

  // NTP servers to connect to
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public List<String> ntpServers = new ArrayList<>();

  // Indicates whether the provider was created before or after PLAT-3009
  // True if it was created after, else it was created before.
  // Dictates whether or not to show the set up NTP option in the provider UI
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public boolean showSetUpChrony = true;

  // Hosted Zone for the deployment
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public String hostedZoneId = null;

  // End Transient Properties

  @JsonProperty("config")
  public void setConfig(Map<String, String> configMap) {
    this.config = configMap;
  }

  @JsonProperty("config")
  public Map<String, String> getMaskedConfig() {
    return maskConfigNew(this.getUnmaskedConfig());
  }

  @JsonIgnore
  public Map<String, String> getUnmaskedConfig() {
    if (config == null) return new HashMap<>();
    return config;
  }

  @JsonIgnore
  public String getYbHome() {
    String ybHomeDir = this.getUnmaskedConfig().getOrDefault("YB_HOME_DIR", "");
    if (ybHomeDir.isEmpty()) {
      ybHomeDir = DEFAULT_YB_HOME_DIR;
    }
    return ybHomeDir;
  }

  /** Query Helper for Provider with uuid */
  public static final Finder<UUID, Provider> find = new Finder<UUID, Provider>(Provider.class) {};

  /**
   * Create a new Cloud Provider
   *
   * @param customerUUID, customer uuid
   * @param code, code of cloud provider
   * @param name, name of cloud provider
   * @return instance of cloud provider
   */
  public static Provider create(UUID customerUUID, Common.CloudType code, String name) {
    return create(customerUUID, code, name, new HashMap<>());
  }

  /**
   * Create a new Cloud Provider
   *
   * @param customerUUID, customer uuid
   * @param code, code of cloud provider
   * @param name, name of cloud provider
   * @param config, Map of cloud provider configuration
   * @return instance of cloud provider
   */
  public static Provider create(
      UUID customerUUID, Common.CloudType code, String name, Map<String, String> config) {
    return create(customerUUID, null, code, name, config);
  }

  public static Provider create(
      UUID customerUUID,
      UUID providerUUID,
      Common.CloudType code,
      String name,
      Map<String, String> config) {
    Provider provider = new Provider();
    provider.customerUUID = customerUUID;
    provider.uuid = providerUUID;
    provider.code = code.toString();
    provider.name = name;
    provider.setConfig(config);
    provider.save();
    return provider;
  }

  /**
   * Query provider based on customer uuid and provider uuid
   *
   * @param customerUUID, customer uuid
   * @param providerUUID, cloud provider uuid
   * @return instance of cloud provider.
   */
  public static Provider get(UUID customerUUID, UUID providerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).idEq(providerUUID).findOne();
  }

  public static Provider getOrBadRequest(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.get(customerUUID, providerUUID);
    if (provider == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Provider UUID: " + providerUUID);
    }
    return provider;
  }

  /**
   * Get all the providers for a given customer uuid
   *
   * @param customerUUID, customer uuid
   * @return list of cloud providers.
   */
  public static List<Provider> getAll(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  /**
   * Get a list of providers filtered by name and code (if not null) for a given customer uuid
   *
   * @param customerUUID
   * @param name
   * @return
   */
  public static List<Provider> getAll(UUID customerUUID, String name, Common.CloudType code) {
    ExpressionList<Provider> query = find.query().where().eq("customer_uuid", customerUUID);
    if (name != null) {
      query.eq("name", name);
    }
    if (code != null) {
      query.eq("code", code.toString());
    }
    return query.findList();
  }

  /**
   * Get Provider by code for a given customer uuid. If there is multiple providers with the same
   * name, it will raise a exception.
   *
   * @param customerUUID
   * @param code
   * @return
   */
  public static List<Provider> get(UUID customerUUID, Common.CloudType code) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("code", code.toString())
        .findList();
  }

  /**
   * Get Provider by name, cloud for a given customer uuid. If there is multiple providers with the
   * same name, cloud will raise a exception.
   *
   * @param customerUUID
   * @param name
   * @param code
   * @return
   */
  public static Provider get(UUID customerUUID, String name, Common.CloudType code) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("name", name)
        .eq("code", code.toString())
        .findOne();
  }

  // Use get Or bad request
  @Deprecated
  public static Provider get(UUID providerUuid) {
    return find.byId(providerUuid);
  }

  public static Provider getOrBadRequest(UUID providerUuid) {
    Provider provider = find.byId(providerUuid);
    if (provider == null)
      throw new PlatformServiceException(BAD_REQUEST, "Cannot find provider " + providerUuid);
    return provider;
  }

  @ApiModelProperty(required = false)
  public String getHostedZoneId() {
    return getUnmaskedConfig()
        .getOrDefault("HOSTED_ZONE_ID", getUnmaskedConfig().get("AWS_HOSTED_ZONE_ID"));
  }

  @ApiModelProperty(required = false)
  public String getHostedZoneName() {
    return getUnmaskedConfig()
        .getOrDefault("HOSTED_ZONE_NAME", getUnmaskedConfig().get("AWS_HOSTED_ZONE_NAME"));
  }

  /**
   * Get all Providers by code without customer uuid.
   *
   * @param code
   * @return
   */
  public static List<Provider> getByCode(String code) {
    return find.query().where().eq("code", code).findList();
  }

  // Update host zone.
  public void updateHostedZone(String hostedZoneId, String hostedZoneName) {
    Map<String, String> currentProviderConfig = getUnmaskedConfig();
    currentProviderConfig.put("HOSTED_ZONE_ID", hostedZoneId);
    currentProviderConfig.put("HOSTED_ZONE_NAME", hostedZoneName);
    this.setConfig(currentProviderConfig);
    this.save();
  }

  // Used for GCP providers to pass down region information. Currently maps regions to
  // their subnets. Only user-input fields should be retrieved here (e.g. zones should
  // not be included for GCP because they're generated from devops).
  @JsonIgnore
  public CloudBootstrap.Params getCloudParams() {
    CloudBootstrap.Params newParams = new CloudBootstrap.Params();
    newParams.perRegionMetadata = new HashMap<>();
    if (!this.code.equals(Common.CloudType.gcp.toString())) {
      return newParams;
    }

    List<Region> regions = Region.getByProvider(this.uuid);
    if (regions == null || regions.isEmpty()) {
      return newParams;
    }

    for (Region r : regions) {
      List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(r.uuid);
      if (zones == null || zones.isEmpty()) {
        continue;
      }
      PerRegionMetadata regionData = new PerRegionMetadata();
      // For GCP, a subnet is assigned to each region, so we only need the first zone's subnet.
      regionData.subnetId = zones.get(0).subnet;
      newParams.perRegionMetadata.put(r.code, regionData);
    }
    return newParams;
  }
}
