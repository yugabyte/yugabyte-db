/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.migrations;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonManagedReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AccessKey.MigratedKeyInfoFields;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import com.yugabyte.yw.models.helpers.provider.DefaultCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.OnPremCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AWSRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.DefaultRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import com.yugabyte.yw.models.helpers.provider.region.azs.DefaultAZCloudInfo;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import jakarta.persistence.CascadeType;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.OneToMany;
import jakarta.persistence.Table;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

/** Snapshot View of ORM entities at the time migration V231 was added. */
@Slf4j
public class V231 {

  public static final ObjectMapper mapper = Json.mapper();

  @Entity
  @Table(name = "customer")
  @Getter
  @Setter
  public static class Customer extends Model {

    @Id public UUID uuid;

    public static final Finder<UUID, Customer> find = new Finder<UUID, Customer>(Customer.class) {};
  }

  @Entity
  @Table(name = "provider")
  @Getter
  @Setter
  public static class Provider extends Model {

    @Id public UUID uuid;

    @Column(name = "customer_uuid", nullable = false)
    public UUID customerUUID;

    @Column(nullable = false, columnDefinition = "TEXT")
    @DbJson
    @Encrypted
    private Map<String, String> config;

    @Column(nullable = false)
    private String code;

    @Column(nullable = false, columnDefinition = "TEXT")
    @Encrypted
    @DbJson
    public ProviderDetails details = new ProviderDetails();

    public CloudType getCloudCode() {
      return CloudType.valueOf(this.getCode());
    }

    @OneToMany(cascade = CascadeType.ALL)
    @JsonManagedReference(value = "provider-regions")
    private List<Region> regions;

    public static final Finder<UUID, Provider> find = new Finder<UUID, Provider>(Provider.class) {};

    public static List<Provider> getAll(UUID customerUUID) {
      return find.query().where().eq("customer_uuid", customerUUID).findList();
    }
  }

  @Data
  @EqualsAndHashCode(callSuper = false)
  public static class ProviderDetails extends MigratedKeyInfoFields {

    @Data
    @JsonInclude(JsonInclude.Include.NON_NULL)
    public static class CloudInfo {
      public AWSCloudInfo aws;
      public AzureCloudInfo azu;
      public GCPCloudInfo gcp;
      public KubernetesInfo kubernetes;
      public OnPremCloudInfo onprem;
    }

    public CloudInfo cloudInfo;
  }

  @Entity
  @Table(name = "region")
  @Getter
  @Setter
  public static class Region extends Model {
    @Id public UUID uuid;

    @Column(length = 25, nullable = false)
    public String code;

    public String ybImage;

    @Column(nullable = false)
    @ManyToOne
    @JsonBackReference("provider-regions")
    public Provider provider;

    @Encrypted
    @DbJson
    @Column(columnDefinition = "TEXT")
    public RegionDetails details = new RegionDetails();

    public void setYbImage(String ybImage) {
      CloudType cloudType = this.provider.getCloudCode();
      if (cloudType.equals(CloudType.aws)) {
        AWSRegionCloudInfo regionCloudInfo = CloudInfoInterface_Clone.get(this);
        regionCloudInfo.setYbImage(ybImage);
      } else if (cloudType.equals(CloudType.gcp)) {
        GCPRegionCloudInfo regionCloudInfo = CloudInfoInterface_Clone.get(this);
        regionCloudInfo.setYbImage(ybImage);
      } else if (cloudType.equals(CloudType.azu)) {
        AzureRegionCloudInfo regionCloudInfo = CloudInfoInterface_Clone.get(this);
        regionCloudInfo.setYbImage(ybImage);
      }
    }

    public void setSecurityGroupId(String securityGroupId) {
      CloudType cloudType = this.provider.getCloudCode();
      if (cloudType == CloudType.aws) {
        AWSRegionCloudInfo regionCloudInfo = CloudInfoInterface_Clone.get(this);
        regionCloudInfo.setSecurityGroupId(securityGroupId);
      } else if (cloudType == CloudType.azu) {
        AzureRegionCloudInfo regionCloudInfo = CloudInfoInterface_Clone.get(this);
        regionCloudInfo.setSecurityGroupId(securityGroupId);
      }
    }

    public void setVnetName(String vnetName) {
      CloudType cloudType = this.provider.getCloudCode();
      if (cloudType.equals(CloudType.aws)) {
        AWSRegionCloudInfo regionCloudInfo = CloudInfoInterface_Clone.get(this);
        regionCloudInfo.setVnet(vnetName);
      } else if (cloudType.equals(CloudType.azu)) {
        AzureRegionCloudInfo regionCloudInfo = CloudInfoInterface_Clone.get(this);
        regionCloudInfo.setVnet(vnetName);
      }
    }

    public void setArchitecture(Architecture arch) {
      CloudType cloudType = this.provider.getCloudCode();
      if (cloudType.equals(CloudType.aws)) {
        AWSRegionCloudInfo regionCloudInfo = CloudInfoInterface_Clone.get(this);
        regionCloudInfo.setArch(arch);
      }
    }

    @OneToMany(cascade = CascadeType.ALL)
    @JsonManagedReference("region-zones")
    private List<AvailabilityZone> zones;
  }

  @Data
  public static class RegionDetails {
    public String sg_id;
    public String vnet;
    public Architecture arch;

    @Data
    @JsonInclude(JsonInclude.Include.NON_NULL)
    public static class RegionCloudInfo {
      public AWSRegionCloudInfo aws;
      public AzureRegionCloudInfo azu;
      public GCPRegionCloudInfo gcp;
      public KubernetesRegionInfo kubernetes;
    }

    public RegionCloudInfo cloudInfo;
  }

  @Entity
  @Table(name = "availability_zone")
  @Getter
  @Setter
  public static class AvailabilityZone extends Model {

    @Id public UUID uuid;

    @Column(length = 25, nullable = false)
    public String code;

    @Column(nullable = false)
    @ManyToOne
    @JsonBackReference("region-zones")
    public Region region;

    @DbJson
    @Column(columnDefinition = "TEXT")
    public Map<String, String> config;

    @Encrypted
    @DbJson
    @Column(columnDefinition = "TEXT")
    public AvailabilityZoneDetails details = new AvailabilityZoneDetails();

    public CloudType getProviderCloudCode() {
      if (region != null) {
        return region.provider.getCloudCode();
      }

      return CloudType.other;
    }
  }

  @Data
  public static class AvailabilityZoneDetails {

    @Data
    @JsonInclude(JsonInclude.Include.NON_NULL)
    public static class AZCloudInfo {
      public KubernetesRegionInfo kubernetes;
    }

    public AZCloudInfo cloudInfo;
  }

  // Simulating the CloudInfoInterface as class as we need few helpers
  // as part of this migration.
  public static class CloudInfoInterface_Clone {

    public static void setCloudProviderInfoFromConfig(
        Provider provider, Map<String, String> config) {
      ProviderDetails providerDetails = provider.getDetails();
      if (providerDetails == null) {
        providerDetails = new ProviderDetails();
        provider.details = providerDetails;
      }
      ProviderDetails.CloudInfo cloudInfo = providerDetails.getCloudInfo();
      if (cloudInfo == null) {
        cloudInfo = new ProviderDetails.CloudInfo();
        providerDetails.setCloudInfo(cloudInfo);
      }
      CloudType cloudType = provider.getCloudCode();
      setFromConfig(cloudInfo, config, cloudType);
    }

    public static void setCloudProviderInfoFromConfig(
        AvailabilityZone az, Map<String, String> config) {
      CloudType cloudType = az.getProviderCloudCode();
      if (cloudType.equals(CloudType.other)) {
        return;
      }
      AvailabilityZoneDetails azDetails = az.getDetails();
      if (azDetails == null) {
        azDetails = new AvailabilityZoneDetails();
        az.details = azDetails;
      }
      AvailabilityZoneDetails.AZCloudInfo cloudInfo = azDetails.getCloudInfo();
      if (cloudInfo == null) {
        cloudInfo = new AvailabilityZoneDetails.AZCloudInfo();
        azDetails.setCloudInfo(cloudInfo);
      }
      setFromConfig(cloudInfo, config, cloudType);
    }

    public static void setFromConfig(
        ProviderDetails.CloudInfo cloudInfo, Map<String, String> config, CloudType cloudType) {

      if (config == null) {
        return;
      }

      switch (cloudType) {
        case aws:
          AWSCloudInfo awsCloudInfo = mapper.convertValue(config, AWSCloudInfo.class);
          cloudInfo.setAws(awsCloudInfo);
          break;
        case gcp:
          GCPCloudInfo gcpCloudInfo = mapper.convertValue(config, GCPCloudInfo.class);
          cloudInfo.setGcp(gcpCloudInfo);
          break;
        case azu:
          AzureCloudInfo azuCloudInfo = mapper.convertValue(config, AzureCloudInfo.class);
          cloudInfo.setAzu(azuCloudInfo);
          break;
        case kubernetes:
          KubernetesInfo kubernetesInfo = mapper.convertValue(config, KubernetesInfo.class);
          cloudInfo.setKubernetes(kubernetesInfo);
          break;
        case onprem:
          OnPremCloudInfo onPremCloudInfo = mapper.convertValue(config, OnPremCloudInfo.class);
          cloudInfo.setOnprem(onPremCloudInfo);
          break;
        case local:
          // TODO: check if it used anymore? in case not, remove the local universe case
          // Import Universe case
          break;
        default:
          throw new PlatformServiceException(BAD_REQUEST, "Unsupported cloud type");
      }
    }

    public static void setFromConfig(
        AvailabilityZoneDetails.AZCloudInfo cloudInfo,
        Map<String, String> config,
        CloudType cloudType) {
      if (config == null) {
        return;
      }

      switch (cloudType) {
        case kubernetes:
          KubernetesRegionInfo kubernetesAZInfo =
              mapper.convertValue(config, KubernetesRegionInfo.class);
          cloudInfo.setKubernetes(kubernetesAZInfo);
          break;
        default:
          break;
      }
    }

    public static <T extends CloudInfoInterface> T get(Provider provider) {
      return get(provider, false);
    }

    public static <T extends CloudInfoInterface> T get(Region region) {
      return get(region, false);
    }

    public static <T extends CloudInfoInterface> T get(AvailabilityZone zone) {
      return get(zone, false);
    }

    public static <T extends CloudInfoInterface> T get(
        Provider provider, Boolean maskSensitiveData) {
      ProviderDetails providerDetails = provider.getDetails();
      if (providerDetails == null) {
        providerDetails = new ProviderDetails();
        provider.details = providerDetails;
      }
      CloudType cloudType = provider.getCloudCode();
      return get(providerDetails, maskSensitiveData, cloudType);
    }

    public static <T extends CloudInfoInterface> T get(
        ProviderDetails providerDetails, Boolean maskSensitiveData, CloudType cloudType) {
      ProviderDetails.CloudInfo cloudInfo = providerDetails.getCloudInfo();
      if (cloudInfo == null) {
        cloudInfo = new ProviderDetails.CloudInfo();
        providerDetails.cloudInfo = cloudInfo;
      }
      return getCloudInfo(cloudInfo, cloudType, maskSensitiveData);
    }

    public static <T extends CloudInfoInterface> T get(Region region, Boolean maskSensitiveData) {
      RegionDetails regionDetails = region.getDetails();
      if (regionDetails == null) {
        regionDetails = new RegionDetails();
        region.details = regionDetails;
      }
      CloudType cloudType = region.provider.getCloudCode();
      return get(regionDetails, maskSensitiveData, cloudType);
    }

    public static <T extends CloudInfoInterface> T get(
        RegionDetails regionDetails, Boolean maskSensitiveData, CloudType cloudType) {
      RegionDetails.RegionCloudInfo cloudInfo = regionDetails.getCloudInfo();
      if (cloudInfo == null) {
        cloudInfo = new RegionDetails.RegionCloudInfo();
        regionDetails.cloudInfo = cloudInfo;
      }
      return getCloudInfo(cloudInfo, cloudType, maskSensitiveData);
    }

    public static <T extends CloudInfoInterface> T get(
        AvailabilityZone zone, Boolean maskSensitiveData) {
      AvailabilityZoneDetails azDetails = zone.getDetails();
      if (azDetails == null) {
        azDetails = new AvailabilityZoneDetails();
        zone.details = azDetails;
      }
      CloudType cloudType = zone.getProviderCloudCode();
      return get(azDetails, maskSensitiveData, cloudType);
    }

    public static <T extends CloudInfoInterface> T get(
        AvailabilityZoneDetails azDetails, Boolean maskSensitiveData, CloudType cloudType) {
      AvailabilityZoneDetails.AZCloudInfo cloudInfo = azDetails.getCloudInfo();
      if (cloudInfo == null) {
        cloudInfo = new AvailabilityZoneDetails.AZCloudInfo();
        azDetails.cloudInfo = cloudInfo;
      }
      return getCloudInfo(cloudInfo, cloudType, maskSensitiveData);
    }

    public static <T extends CloudInfoInterface> T getCloudInfo(
        ProviderDetails.CloudInfo cloudInfo, CloudType cloudType, Boolean maskSensitiveData) {
      switch (cloudType) {
        case aws:
          AWSCloudInfo awsCloudInfo = cloudInfo.getAws();
          if (awsCloudInfo == null) {
            awsCloudInfo = new AWSCloudInfo();
            cloudInfo.setAws(awsCloudInfo);
          }
          if (awsCloudInfo != null && maskSensitiveData) {
            awsCloudInfo.withSensitiveDataMasked();
          }
          return (T) awsCloudInfo;
        case gcp:
          GCPCloudInfo gcpCloudInfo = cloudInfo.getGcp();
          if (gcpCloudInfo == null) {
            gcpCloudInfo = new GCPCloudInfo();
            cloudInfo.setGcp(gcpCloudInfo);
          }
          if (gcpCloudInfo != null && maskSensitiveData) {
            gcpCloudInfo.withSensitiveDataMasked();
          }
          return (T) gcpCloudInfo;
        case azu:
          AzureCloudInfo azuCloudInfo = cloudInfo.getAzu();
          if (azuCloudInfo == null) {
            azuCloudInfo = new AzureCloudInfo();
            cloudInfo.setAzu(azuCloudInfo);
          }
          if (azuCloudInfo != null && maskSensitiveData) {
            azuCloudInfo.withSensitiveDataMasked();
          }
          return (T) azuCloudInfo;
        case kubernetes:
          KubernetesInfo kubernetesInfo = cloudInfo.getKubernetes();
          if (kubernetesInfo == null) {
            kubernetesInfo = new KubernetesInfo();
            cloudInfo.setKubernetes(kubernetesInfo);
          }
          if (kubernetesInfo != null && maskSensitiveData) {
            kubernetesInfo.withSensitiveDataMasked();
          }
          return (T) kubernetesInfo;
        case onprem:
          OnPremCloudInfo onpremCloudInfo = cloudInfo.getOnprem();
          if (onpremCloudInfo == null) {
            onpremCloudInfo = new OnPremCloudInfo();
            cloudInfo.setOnprem(onpremCloudInfo);
          }
          if (onpremCloudInfo != null && maskSensitiveData) {
            onpremCloudInfo.withSensitiveDataMasked();
          }
          return (T) onpremCloudInfo;
        default:
          // Placeholder. Don't want consumers to receive null.
          return (T) new DefaultCloudInfo();
      }
    }

    public static <T extends CloudInfoInterface> T getCloudInfo(
        RegionDetails.RegionCloudInfo cloudInfo, CloudType cloudType, Boolean maskSensitiveData) {
      switch (cloudType) {
        case aws:
          AWSRegionCloudInfo awsRegionCloudInfo = cloudInfo.getAws();
          if (awsRegionCloudInfo == null) {
            awsRegionCloudInfo = new AWSRegionCloudInfo();
            cloudInfo.setAws(awsRegionCloudInfo);
          }
          if (awsRegionCloudInfo != null && maskSensitiveData) {
            awsRegionCloudInfo.withSensitiveDataMasked();
          }
          return (T) awsRegionCloudInfo;
        case gcp:
          GCPRegionCloudInfo gcpRegionCloudInfo = cloudInfo.getGcp();
          if (gcpRegionCloudInfo == null) {
            gcpRegionCloudInfo = new GCPRegionCloudInfo();
            cloudInfo.setGcp(gcpRegionCloudInfo);
          }
          if (gcpRegionCloudInfo != null && maskSensitiveData) {
            gcpRegionCloudInfo.withSensitiveDataMasked();
          }
          return (T) gcpRegionCloudInfo;
        case azu:
          AzureRegionCloudInfo azuRegionCloudInfo = cloudInfo.getAzu();
          if (azuRegionCloudInfo == null) {
            azuRegionCloudInfo = new AzureRegionCloudInfo();
            cloudInfo.setAzu(azuRegionCloudInfo);
          }
          if (azuRegionCloudInfo != null && maskSensitiveData) {
            azuRegionCloudInfo.withSensitiveDataMasked();
          }
          return (T) azuRegionCloudInfo;
        case kubernetes:
          KubernetesRegionInfo kubernetesInfo = cloudInfo.getKubernetes();
          if (kubernetesInfo == null) {
            kubernetesInfo = new KubernetesRegionInfo();
            cloudInfo.setKubernetes(kubernetesInfo);
          }
          if (kubernetesInfo != null && maskSensitiveData) {
            kubernetesInfo.withSensitiveDataMasked();
          }
          return (T) kubernetesInfo;
        default:
          // Placeholder. Don't want consumers to receive null.
          return (T) new DefaultRegionCloudInfo();
      }
    }

    public static <T extends CloudInfoInterface> T getCloudInfo(
        AvailabilityZoneDetails.AZCloudInfo cloudInfo,
        CloudType cloudType,
        Boolean maskSensitiveData) {
      switch (cloudType) {
        case kubernetes:
          KubernetesRegionInfo kubernetesAZInfo = cloudInfo.getKubernetes();
          if (kubernetesAZInfo == null) {
            kubernetesAZInfo = new KubernetesRegionInfo();
            cloudInfo.setKubernetes(kubernetesAZInfo);
          }
          if (kubernetesAZInfo != null && maskSensitiveData) {
            kubernetesAZInfo.withSensitiveDataMasked();
          }
          return (T) kubernetesAZInfo;
        default:
          return (T) new DefaultAZCloudInfo();
      }
    }
  }
}
