// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.DEFAULT_YB_HOME_DIR;
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
import com.yugabyte.yw.models.common.YBADeprecated;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;

import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import io.swagger.annotations.ApiModelProperty;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
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
import javax.persistence.Version;
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

  @YBADeprecated(sinceDate = "2023-02-11", sinceYBAVersion = "2.17.2.0")
  @ApiModelProperty(
      value =
          "Deprecated: sinceDate=2023-02-11, sinceYBAVersion=2.17.2.0, "
              + "Use details.metadata instead")
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  @Encrypted
  public Map<String, String> config;

  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  @Encrypted
  public ProviderDetails details = new ProviderDetails();

  @OneToMany(cascade = CascadeType.ALL)
  @JsonManagedReference(value = "provider-regions")
  public List<Region> regions;

  @ApiModelProperty(required = false)
  @OneToMany(cascade = CascadeType.ALL)
  @JsonManagedReference(value = "provider-accessKey")
  public List<AccessKey> allAccessKeys;

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
  @YBADeprecated(sinceDate = "2023-02-11", sinceYBAVersion = "2.17.2.0")
  @Transient
  @ApiModelProperty(
      value =
          "Deprecated: sinceDate=2023-02-11, sinceYBAVersion=2.17.2.0, "
              + "Use allAccessKeys[0].keyInfo.keyPairName instead")
  public String keyPairName = null;

  // Custom SSH private key component.
  // Default: created and managed by YB.
  @YBADeprecated(sinceDate = "2023-02-11", sinceYBAVersion = "2.17.2.0")
  @Transient
  @ApiModelProperty(
      value =
          "Deprecated: sinceDate=2023-02-11, sinceYBAVersion=2.17.2.0, "
              + "Use allAccessKeys[0].keyInfo.sshPrivateKeyContent instead")
  public String sshPrivateKeyContent = null;

  @Deprecated
  @JsonProperty("keyPairName")
  public void setKeyPairName(String keyPairName) {
    if (this.allAccessKeys.size() > 0) {
      this.allAccessKeys.get(0).getKeyInfo().keyPairName = keyPairName;
    } else {
      AccessKey accessKey = new AccessKey();
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.keyPairName = keyPairName;
      accessKey.setKeyInfo(keyInfo);
      this.allAccessKeys.add(accessKey);
    }
  }

  @Deprecated
  @JsonProperty("sshPrivateKeyContent")
  public void setSshPrivateKeyContent(String sshPrivateKeyContent) {
    if (this.allAccessKeys.size() > 0) {
      this.allAccessKeys.get(0).getKeyInfo().sshPrivateKeyContent = sshPrivateKeyContent;
    } else {
      AccessKey accessKey = new AccessKey();
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.sshPrivateKeyContent = sshPrivateKeyContent;
      accessKey.setKeyInfo(keyInfo);
      this.allAccessKeys.add(accessKey);
    }
  }

  // Custom SSH user to login to machines.
  // Default: created and managed by YB.
  @YBADeprecated(sinceDate = "2023-02-11", sinceYBAVersion = "2.17.2.0")
  @Transient
  @ApiModelProperty(
      value =
          "Deprecated: sinceDate=2023-02-11, sinceYBAVersion=2.17.2.0, "
              + "Use details.SshUser instead")
  public String sshUser = null;

  // Custom SSH user to login to machines.
  // Default: created and managed by YB.
  public void setSshUser(String sshUser) {
    this.details.sshUser = sshUser;
  }

  // Port to open for connections on the instance.
  @YBADeprecated(sinceDate = "2023-02-11", sinceYBAVersion = "2.17.2.0")
  @ApiModelProperty(
      value =
          "Deprecated: sinceDate=2023-02-11, sinceYBAVersion=2.17.2.0, "
              + "Use details.SshPort instead")
  public Integer getSshPort() {
    return this.details.sshPort;
  }

  public void setSshPort(Integer sshPort) {
    this.details.sshPort = sshPort;
  }

  // Whether provider should use airgapped install.
  // Default: false.
  @YBADeprecated(sinceDate = "2023-02-11", sinceYBAVersion = "2.17.2.0")
  @Transient
  @ApiModelProperty(
      value =
          "Deprecated: sinceDate=2023-02-11, sinceYBAVersion=2.17.2.0, "
              + "Use details.airGapInstall")
  public boolean airGapInstall = false;

  // Whether provider should use airgapped install. Default: false.
  public void setAirGapInstall(boolean v) {
    details.airGapInstall = v;
  }

  @Deprecated
  @ApiModelProperty(hidden = true)
  public void setNtpServers(List<String> ntpServers) {
    this.details.ntpServers = ntpServers;
  }

  /**
   * Whether or not to set up NTP
   *
   * @deprecated use details.setUpChrony
   */
  @Deprecated
  @ApiModelProperty(hidden = true)
  public void setSetUpChrony(boolean v) {
    details.setUpChrony = v;
  }

  /**
   * Indicates whether the provider was created before or after PLAT-3009 True if it was created
   * after, else it was created before. Dictates whether or not to show the set up NTP option in the
   * provider UI
   */
  @Deprecated
  @ApiModelProperty(hidden = true)
  public void setShowSetUpChrony(boolean showSetUpChrony) {
    this.details.showSetUpChrony = showSetUpChrony;
  }

  // Moving below 3 fields back to transient as they were previously.
  // Migration for these fields is not required as we started persisting
  // these fields recently only as part of v2 APIs only.
  // UI only calls passes these values in the bootstrap call.
  @Deprecated @Transient @ApiModelProperty public String hostVpcId = null;

  @Deprecated @Transient @ApiModelProperty public String hostVpcRegion = null;

  @Deprecated @Transient @ApiModelProperty public String destVpcId = null;

  // Hosted Zone for the deployment
  @Deprecated
  @Transient
  @ApiModelProperty(TRANSIENT_PROPERTY_IN_MUTATE_API_REQUEST)
  public String hostedZoneId = null;

  // End Transient Properties

  @Column(nullable = false)
  @Version
  @ApiModelProperty(value = "Provider version", accessMode = READ_ONLY)
  private long version;

  public long getVersion() {
    return version;
  }

  public void setVersion(long version) {
    this.version = version;
  }

  @Deprecated
  @JsonProperty("config")
  public void setConfig(Map<String, String> configMap) {
    if (configMap != null && !configMap.isEmpty()) {
      CloudInfoInterface.setCloudProviderInfoFromConfig(this, configMap);
    }
  }

  @JsonProperty("details")
  public void setProviderDetails(ProviderDetails providerDetails) {
    this.details = providerDetails;
  }

  @JsonProperty("details")
  public ProviderDetails getMaskProviderDetails() {
    return CloudInfoInterface.maskProviderDetails(this);
  }

  @JsonIgnore
  public ProviderDetails getProviderDetails() {
    if (details == null) {
      details = new ProviderDetails();
    }
    return details;
  }

  @JsonIgnore
  public String getYbHome() {
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(this);
    String ybHomeDir = config.getOrDefault("YB_HOME_DIR", "");
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
  @Deprecated
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
  @Deprecated
  public static Provider create(
      UUID customerUUID, Common.CloudType code, String name, Map<String, String> config) {
    return create(customerUUID, null, code, name, config);
  }

  @Deprecated
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
    provider.details = new ProviderDetails();
    provider.setConfig(config);
    provider.save();
    return provider;
  }

  /**
   * Create a new Cloud Provider
   *
   * @param customerUUID, customer uuid
   * @param code, code of cloud provider
   * @param name, name of cloud provider
   * @param providerDetails, providerDetails configuration.
   * @return instance of cloud provider
   */
  public static Provider create(
      UUID customerUUID, Common.CloudType code, String name, ProviderDetails providerDetails) {
    return create(customerUUID, null, code, name, providerDetails);
  }

  public static Provider create(
      UUID customerUUID,
      UUID providerUUID,
      Common.CloudType code,
      String name,
      ProviderDetails providerDetails) {
    Provider provider = new Provider();
    provider.customerUUID = customerUUID;
    provider.uuid = providerUUID;
    provider.code = code.toString();
    provider.name = name;
    provider.details = providerDetails;
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

  public static Optional<Provider> maybeGet(UUID providerUUID) {
    // Find the Provider.
    Provider provider = find.byId(providerUUID);
    if (provider == null) {
      LOG.trace("Cannot find provider {}", providerUUID);
      return Optional.empty();
    }

    // Return the provider object.
    return Optional.of(provider);
  }

  public static Provider getOrBadRequest(UUID providerUuid) {
    Provider provider = find.byId(providerUuid);
    if (provider == null)
      throw new PlatformServiceException(BAD_REQUEST, "Cannot find provider " + providerUuid);
    return provider;
  }

  @ApiModelProperty(required = false, hidden = true)
  public String getHostedZoneId() {
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(this);
    return config.getOrDefault("HOSTED_ZONE_ID", null);
  }

  @ApiModelProperty(required = false, hidden = true)
  public String getHostedZoneName() {
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(this);
    return config.getOrDefault("HOSTED_ZONE_NAME", null);
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
    if (regions.isEmpty()) {
      return newParams;
    }

    for (Region r : regions) {
      List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(r.uuid);
      if (zones.isEmpty()) {
        continue;
      }
      PerRegionMetadata regionData = new PerRegionMetadata();
      // For GCP, a subnet is assigned to each region, so we only need the first zone's subnet.
      regionData.subnetId = zones.get(0).subnet;
      newParams.perRegionMetadata.put(r.code, regionData);
    }
    return newParams;
  }

  @JsonIgnore
  public long getUniverseCount() {
    return Customer.get(this.customerUUID).getUniversesForProvider(this.uuid).stream().count();
  }
}
