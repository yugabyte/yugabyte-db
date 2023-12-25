// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.DB;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Junction;
import io.ebean.Model;
import io.ebean.SqlUpdate;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import javax.persistence.FetchType;
import javax.persistence.JoinColumn;
import javax.persistence.ManyToOne;
import lombok.Getter;
import lombok.Setter;
import org.apache.commons.collections4.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

@ApiModel(description = "Information about an instance")
@Entity
@Getter
@Setter
public class InstanceType extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(InstanceType.class);

  private static final List<String> AWS_INSTANCE_PREFIXES_SUPPORTED =
      ImmutableList.of("m3.", "c5.", "c5d.", "c4.", "c3.", "i3.");
  private static final List<String> GRAVITON_AWS_INSTANCE_PREFIXES_SUPPORTED =
      ImmutableList.of("m6g.", "c6gd.", "c6g.", "t4g.");
  private static final List<String> CLOUD_AWS_INSTANCE_PREFIXES_SUPPORTED =
      ImmutableList.of(
          "m3.", "c5.", "c5d.", "c4.", "c3.", "i3.", "t2.", "t3.", "t4g.", "m6i.", "m5.");

  static final String YB_AWS_DEFAULT_VOLUME_COUNT_KEY = "yb.aws.default_volume_count";
  static final String YB_AWS_DEFAULT_VOLUME_SIZE_GB_KEY = "yb.aws.default_volume_size_gb";

  public enum VolumeType {
    @EnumValue("EBS")
    EBS,

    @EnumValue("SSD")
    SSD,

    @EnumValue("HDD")
    HDD,

    @EnumValue("NVME")
    NVME
  }

  @Getter @Setter @EmbeddedId @Constraints.Required private InstanceTypeKey idKey;

  // ManyToOne for provider is kept outside of InstanceTypeKey
  // as ebean currently doesn't support having @ManyToOne inside @EmbeddedId
  // insertable and updatable are set to false as actual updates
  // are taken care by providerUuid parameter in InstanceTypeKey
  // This is currently not used directly, but in order for ebean to be able
  // to save instances of this model, this association has to be bidirectional
  @ManyToOne(optional = false, fetch = FetchType.LAZY)
  @JoinColumn(name = "provider_uuid", insertable = false, updatable = false)
  @JsonIgnore
  private Provider provider;

  @ApiModelProperty(value = "True if the instance is active", accessMode = READ_ONLY)
  @Constraints.Required
  @Column(nullable = false, columnDefinition = "boolean default true")
  @Getter
  @Setter
  private boolean active = true;

  @ApiModelProperty(value = "The instance's number of CPU cores", accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false, columnDefinition = "float")
  private Double numCores;

  @ApiModelProperty(value = "The instance's memory size, in gigabytes", accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false, columnDefinition = "float")
  private Double memSizeGB;

  @ApiModelProperty(
      value = "Extra details about the instance (as a JSON object)",
      accessMode = READ_WRITE)
  @DbJson
  private InstanceTypeDetails instanceTypeDetails = new InstanceTypeDetails();

  @ApiModelProperty(value = "Instance type code", accessMode = READ_ONLY)
  public String getInstanceTypeCode() {
    return this.idKey.getInstanceTypeCode();
  }

  private static final Finder<InstanceTypeKey, InstanceType> find =
      new Finder<InstanceTypeKey, InstanceType>(InstanceType.class) {};

  public static InstanceType get(UUID providerUuid, String instanceTypeCode) {
    return find.byId(InstanceTypeKey.create(instanceTypeCode, providerUuid));
  }

  public static List<InstanceType> findByKeys(Collection<InstanceTypeKey> keys) {
    if (CollectionUtils.isEmpty(keys)) {
      return Collections.emptyList();
    }
    Set<InstanceTypeKey> uniqueKeys = new HashSet<>(keys);
    ExpressionList<InstanceType> query = find.query().where();
    Junction<InstanceType> orExpr = query.or();
    for (InstanceTypeKey key : uniqueKeys) {
      Junction<InstanceType> andExpr = orExpr.and();
      andExpr.eq("provider_uuid", key.getProviderUuid());
      andExpr.eq("instance_type_code", key.getInstanceTypeCode());
      orExpr.endAnd();
    }
    return query.endOr().findList();
  }

  public static InstanceType getOrBadRequest(UUID providerUuid, String instanceTypeCode) {
    InstanceType instanceType = InstanceType.get(providerUuid, instanceTypeCode);
    if (instanceType == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Instance type not found: " + instanceTypeCode);
    }
    return instanceType;
  }

  public static InstanceType upsert(
      UUID providerUuid,
      String instanceTypeCode,
      Integer numCores,
      Double memSize,
      InstanceTypeDetails instanceTypeDetails) {
    return upsert(providerUuid, instanceTypeCode, (double) numCores, memSize, instanceTypeDetails);
  }

  public static InstanceType upsert(
      UUID providerUuid,
      String instanceTypeCode,
      Double numCores,
      Double memSize,
      InstanceTypeDetails instanceTypeDetails) {
    InstanceType instanceType = InstanceType.get(providerUuid, instanceTypeCode);
    if (instanceType == null) {
      instanceType = new InstanceType();
      instanceType.idKey = InstanceTypeKey.create(instanceTypeCode, providerUuid);
    } else if (!instanceType.isActive()) {
      instanceType.delete();
      instanceType = new InstanceType();
      instanceType.idKey = InstanceTypeKey.create(instanceTypeCode, providerUuid);
    }

    instanceType.setMemSizeGB(memSize);
    instanceType.setNumCores(numCores);
    instanceType.setInstanceTypeDetails(instanceTypeDetails);
    // Update the in-memory fields.
    instanceType.save();

    return instanceType;
  }

  /**
   * Reset the 'instance_type_details_json' of all rows belonging to a specific provider in this
   * table.
   */
  public static void resetInstanceTypeDetailsForProvider(UUID providerUuid) {
    String updateQuery =
        "UPDATE instance_type "
            + "SET instance_type_details = '{}' WHERE provider_uuid = :providerUuid";
    SqlUpdate update = DB.sqlUpdate(updateQuery).setParameter("providerUuid", providerUuid);
    int modifiedCount = DB.getDefault().execute(update);
    LOG.info("Query [" + updateQuery + "] updated " + modifiedCount + " rows");
    if (modifiedCount == 0) {
      LOG.warn("Failed to update any SQL row");
    }
  }

  /** Delete Instance Types corresponding to given provider */
  public static void deleteInstanceTypesForProvider(Provider provider, Config config) {
    for (InstanceType instanceType : findByProvider(provider, config, true)) {
      instanceType.delete();
    }
  }

  private static Predicate<InstanceType> supportedInstanceTypes(
      List<String> supportedPrefixes, boolean allowUnsupported) {
    return p -> {
      final boolean ret =
          supportedPrefixes.stream().anyMatch(prefix -> p.getInstanceTypeCode().startsWith(prefix));
      if (!ret) {
        LOG.trace("Unsupported prefix for instance type {}", p.getInstanceTypeCode());
        if (allowUnsupported) {
          LOG.warn(
              "Allowing unsupported prefix {} supported prefixes: {}",
              p.getInstanceTypeCode(),
              supportedPrefixes);
          return true;
        }
      }
      return ret;
    };
  }

  private static List<InstanceType> populateDefaultsIfEmpty(
      List<InstanceType> entries, Config config, boolean allowUnsupported) {
    // For AWS, we would filter and show only supported instance prefixes
    entries =
        entries.stream()
            .filter(
                supportedInstanceTypes(getAWSInstancePrefixesSupported(config), allowUnsupported))
            .collect(Collectors.toList());
    for (InstanceType instanceType : entries) {
      if (instanceType.getInstanceTypeDetails() == null) {
        instanceType.setInstanceTypeDetails(new InstanceTypeDetails());
      }
      if (instanceType.getInstanceTypeDetails().volumeDetailsList.isEmpty()) {
        instanceType
            .getInstanceTypeDetails()
            .setVolumeDetailsList(
                config.getInt(YB_AWS_DEFAULT_VOLUME_COUNT_KEY),
                config.getInt(YB_AWS_DEFAULT_VOLUME_SIZE_GB_KEY),
                VolumeType.EBS);
      }
    }
    return entries;
  }

  /** Query Helper to find supported instance types for a given cloud provider. */
  public static List<InstanceType> findByProvider(Provider provider, Config config) {
    return findByProvider(provider, config, false);
  }

  /** Query Helper to find supported instance types for a given cloud provider. */
  public static List<InstanceType> findByProvider(
      Provider provider, Config config, boolean allowUnsupported) {
    List<InstanceType> entries =
        InstanceType.find
            .query()
            .where()
            .eq("provider_uuid", provider.getUuid())
            .eq("active", true)
            .findList();
    if (provider.getCode().equals("aws")) {
      return populateDefaultsIfEmpty(entries, config, allowUnsupported);
    } else {
      return entries;
    }
  }

  public static InstanceType createWithMetadata(
      UUID providerUuid, String instanceTypeCode, JsonNode metadata) {
    return upsert(
        providerUuid,
        instanceTypeCode,
        Integer.parseInt(metadata.get("numCores").toString()),
        Double.parseDouble(metadata.get("memSizeGB").toString()),
        Json.fromJson(metadata.get("instanceTypeDetails"), InstanceTypeDetails.class));
  }

  public static List<String> getAWSInstancePrefixesSupported(Config config) {
    if (config.getBoolean("yb.cloud.enabled")) {
      return CLOUD_AWS_INSTANCE_PREFIXES_SUPPORTED;
    }
    return Stream.concat(
            AWS_INSTANCE_PREFIXES_SUPPORTED.stream(),
            GRAVITON_AWS_INSTANCE_PREFIXES_SUPPORTED.stream())
        .collect(Collectors.toList());
  }

  /** Default details for volumes attached to this instance. */
  public static class VolumeDetails {
    public Integer volumeSizeGB;
    public VolumeType volumeType;
    public String mountPath;
  }

  public static class InstanceTypeDetails {
    public static final int DEFAULT_VOLUME_COUNT = 1;
    public static final int DEFAULT_GCP_VOLUME_SIZE_GB = 375;
    public static final int DEFAULT_AZU_VOLUME_SIZE_GB = 250;

    public List<VolumeDetails> volumeDetailsList = new LinkedList<>();
    public PublicCloudConstants.Tenancy tenancy;

    public void setVolumeDetailsList(int volumeCount, int volumeSizeGB, VolumeType volumeType) {
      for (int i = 0; i < volumeCount; i++) {
        VolumeDetails volumeDetails = new VolumeDetails();
        volumeDetails.volumeSizeGB = volumeSizeGB;
        volumeDetails.volumeType = volumeType;
        volumeDetailsList.add(volumeDetails);
      }
      setDefaultMountPaths();
    }

    public void setDefaultMountPaths() {
      for (int idx = 0; idx < volumeDetailsList.size(); ++idx) {
        volumeDetailsList.get(idx).mountPath = String.format("/mnt/d%d", idx);
      }
    }

    public static InstanceTypeDetails createGCPDefault() {
      InstanceTypeDetails instanceTypeDetails = new InstanceTypeDetails();
      instanceTypeDetails.setVolumeDetailsList(
          DEFAULT_VOLUME_COUNT, DEFAULT_GCP_VOLUME_SIZE_GB, VolumeType.SSD);
      return instanceTypeDetails;
    }

    public static InstanceTypeDetails createAZUDefault() {
      InstanceTypeDetails instanceTypeDetails = new InstanceTypeDetails();
      instanceTypeDetails.setVolumeDetailsList(
          DEFAULT_VOLUME_COUNT, DEFAULT_AZU_VOLUME_SIZE_GB, VolumeType.SSD);
      return instanceTypeDetails;
    }

    public static InstanceTypeDetails createGCPInstanceTypeDetails(VolumeType volumeType) {
      InstanceTypeDetails instanceTypeDetails = new InstanceTypeDetails();
      instanceTypeDetails.setVolumeDetailsList(
          DEFAULT_VOLUME_COUNT, DEFAULT_GCP_VOLUME_SIZE_GB, volumeType);
      return instanceTypeDetails;
    }
  }
}
