// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.migrations;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.OneToMany;
import jakarta.persistence.Table;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Stream;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import org.apache.commons.lang3.StringUtils;

/** Snapshot View of ORM entities at the time migration V435 was added. */
public class V437 {

  @Entity
  @Table(name = "universe")
  @Getter
  @Setter
  public static class Universe extends Model {

    @Id public UUID universeUUID;

    @Column(columnDefinition = "TEXT", nullable = false)
    public String universeDetailsJson;

    @DbJson
    @Column(columnDefinition = "TEXT")
    private Map<String, String> config;

    public static final Finder<UUID, Universe> find = new Finder<UUID, Universe>(Universe.class) {};

    public static List<Universe> getAll() {
      return find.query().findList();
    }

    @JsonIgnore
    public Map<String, String> getConfig() {
      if (config == null) {
        config = new java.util.HashMap<>();
      }
      return config;
    }
  }

  @Entity
  @Table(name = "provider")
  @Getter
  @Setter
  public static class Provider extends Model {

    @Id public UUID uuid;

    @Column(nullable = false)
    private String code;

    @Column(name = "customer_uuid", nullable = false)
    public UUID customerUUID;

    @Column(nullable = false, columnDefinition = "TEXT")
    @DbJson
    @Encrypted
    private Map<String, String> config;

    @Column(nullable = false, columnDefinition = "TEXT")
    @Encrypted
    @DbJson
    public ProviderDetails details = new ProviderDetails();

    @OneToMany private List<Region> regions;

    public static final Finder<UUID, Provider> find = new Finder<UUID, Provider>(Provider.class) {};

    public static Provider getOrBadRequest(UUID uuid) {
      Provider provider = find.byId(uuid);
      if (provider == null) {
        throw new RuntimeException("Provider not found: " + uuid);
      }
      return provider;
    }

    public Map<String, String> getConfig() {
      return config;
    }
  }

  @Getter
  @Setter
  public static class ProviderDetails {
    public ProviderDetails.CloudInfo cloudInfo;

    @Getter
    @Setter
    public static class CloudInfo {
      public KubernetesInfo kubernetes;
    }
  }

  @Getter
  @Setter
  public static class KubernetesInfo {
    private String kubernetesStorageClass;

    public String getKubernetesStorageClass() {
      return kubernetesStorageClass;
    }
  }

  @Entity
  @Table(name = "region")
  @Getter
  @Setter
  public static class Region extends Model {

    @Id public UUID uuid;

    @Column(length = 25, nullable = false)
    public String code;

    @Column(nullable = false)
    @ManyToOne
    public Provider provider;

    @DbJson
    @Column(columnDefinition = "TEXT")
    public Map<String, String> config;

    @Encrypted
    @DbJson
    @Column(columnDefinition = "TEXT")
    public RegionDetails details = new RegionDetails();

    @OneToMany private List<AvailabilityZone> zones;

    public static final Finder<UUID, Region> find = new Finder<UUID, Region>(Region.class) {};

    public static Region getOrBadRequest(UUID uuid) {
      Region region = find.byId(uuid);
      if (region == null) {
        throw new RuntimeException("Region not found: " + uuid);
      }
      return region;
    }

    public Map<String, String> getConfig() {
      return config;
    }
  }

  @Getter
  @Setter
  public static class RegionDetails {
    public RegionDetails.RegionCloudInfo cloudInfo;

    @Getter
    @Setter
    public static class RegionCloudInfo {
      public KubernetesRegionInfo kubernetes;
    }
  }

  @Getter
  @Setter
  public static class KubernetesRegionInfo extends KubernetesInfo {
    private String overrides;

    public String getOverrides() {
      return overrides;
    }
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
    public Region region;

    @DbJson
    @Column(columnDefinition = "TEXT")
    public Map<String, String> config;

    @Encrypted
    @DbJson
    @Column(columnDefinition = "TEXT")
    public AvailabilityZoneDetails details = new AvailabilityZoneDetails();

    public static final Finder<UUID, AvailabilityZone> find =
        new Finder<UUID, AvailabilityZone>(AvailabilityZone.class) {};

    public static AvailabilityZone getOrBadRequest(UUID uuid) {
      AvailabilityZone az = find.byId(uuid);
      if (az == null) {
        throw new RuntimeException("AvailabilityZone not found: " + uuid);
      }
      return az;
    }

    public Map<String, String> getConfig() {
      return config;
    }
  }

  @Getter
  @Setter
  public static class AvailabilityZoneDetails {
    public AvailabilityZoneDetails.AZCloudInfo cloudInfo;

    @Getter
    @Setter
    public static class AZCloudInfo {
      public KubernetesRegionInfo kubernetes;
    }
  }

  // Helper to fetch env vars similar to CloudInfoInterface
  public static class CloudInfoInterfaceHelper {
    public static Map<String, String> fetchEnvVars(Provider provider) {
      // For Kubernetes providers, STORAGE_CLASS might be in config or details
      Map<String, String> result = new java.util.HashMap<>();
      if (provider.getConfig() != null) {
        result.putAll(provider.getConfig());
      }
      // Also check details for kubernetes storage class
      if (provider.getDetails() != null
          && provider.getDetails().getCloudInfo() != null
          && provider.getDetails().getCloudInfo().getKubernetes() != null) {
        KubernetesInfo k8sInfo = provider.getDetails().getCloudInfo().getKubernetes();
        if (k8sInfo.getKubernetesStorageClass() != null) {
          result.put("STORAGE_CLASS", k8sInfo.getKubernetesStorageClass());
        }
      }
      return result;
    }

    public static Map<String, String> fetchEnvVars(Region region) {
      Map<String, String> result = new java.util.HashMap<>();
      if (region.getConfig() != null) {
        result.putAll(region.getConfig());
      }
      // Also check details for kubernetes storage class and overrides
      if (region.getDetails() != null
          && region.getDetails().getCloudInfo() != null
          && region.getDetails().getCloudInfo().getKubernetes() != null) {
        KubernetesRegionInfo k8sInfo = region.getDetails().getCloudInfo().getKubernetes();
        if (k8sInfo.getKubernetesStorageClass() != null) {
          result.put("STORAGE_CLASS", k8sInfo.getKubernetesStorageClass());
        }
        if (k8sInfo.getOverrides() != null) {
          result.put("OVERRIDES", k8sInfo.getOverrides());
        }
      }
      return result;
    }

    public static Map<String, String> fetchEnvVars(AvailabilityZone az) {
      Map<String, String> result = new java.util.HashMap<>();
      if (az.getConfig() != null) {
        result.putAll(az.getConfig());
      }
      // Also check details for kubernetes storage class and overrides
      if (az.getDetails() != null
          && az.getDetails().getCloudInfo() != null
          && az.getDetails().getCloudInfo().getKubernetes() != null) {
        KubernetesRegionInfo k8sInfo = az.getDetails().getCloudInfo().getKubernetes();
        if (k8sInfo.getKubernetesStorageClass() != null) {
          result.put("STORAGE_CLASS", k8sInfo.getKubernetesStorageClass());
        }
        if (k8sInfo.getOverrides() != null) {
          result.put("OVERRIDES", k8sInfo.getOverrides());
        }
      }
      return result;
    }
  }

  @AllArgsConstructor
  public static class AZOverrides {
    public Map<String, PerProcessDetails> perProcess;
  }

  @AllArgsConstructor
  public static class PerProcessDetails {
    public DeviceInfo deviceInfo;
  }

  @AllArgsConstructor
  @NoArgsConstructor
  public static class DeviceInfo {
    public Integer numVolumes;
    public String storageClass;
    public Integer volumeSize;

    public static DeviceInfo defaultTserverDeviceInfo() {
      return new DeviceInfo(2, "", 100);
    }

    public static DeviceInfo defaultMasterDeviceInfo() {
      return new DeviceInfo(1, "", 50);
    }

    public void mergeDeviceInfo(DeviceInfo other) {
      if (other.numVolumes != null) {
        numVolumes = other.numVolumes;
      }
      if (other.volumeSize != null) {
        volumeSize = other.volumeSize;
      }
      if (StringUtils.isNotBlank(other.storageClass)) {
        storageClass = other.storageClass;
      }
    }

    public void unsetFields(DeviceInfo other) {
      if (other.volumeSize != null) {
        this.volumeSize = null;
      }
      if (other.numVolumes != null) {
        this.numVolumes = null;
      }
      if (StringUtils.isNotBlank(other.storageClass)) {
        this.storageClass = null;
      }
    }

    @JsonIgnore
    public boolean allNull() {
      return Stream.of(numVolumes, storageClass, volumeSize).allMatch(Objects::isNull);
    }
  }
}
