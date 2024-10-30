// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.Sets;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Junction;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.EmbeddedId;
import jakarta.persistence.Entity;
import jakarta.persistence.FetchType;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.Setter;
import lombok.ToString;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
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

  // todo: https://yugabyte.atlassian.net/browse/PLAT-10505
  private static final Pattern AZU_NO_LOCAL_DISK =
      Pattern.compile("Standard_(D|E)[0-9]*as\\_v5|Standard_D[0-9]*s\\_v5|Standard_D[0-9]*s\\_v4");

  private static final List<String> AWS_INSTANCE_PREFIXES_SUPPORTED =
      ImmutableList.of(
          "m3.", "c5.", "c5d.", "c4.", "c3.", "i3.", "m4.", "m5.", "m5a.", "m6i.", "m6a.", "m7i.",
          "m7a.", "c6i.", "c6a.", "c7a.", "c7i.");
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

  public static List<InstanceType> getAllInstanceTypes() {
    return find.all();
  }

  public static List<InstanceType> getInstanceTypes(
      UUID providerUuid, Set<String> instanceTypeCodes) {
    return find.query()
        .where()
        .eq("provider_uuid", providerUuid)
        .in("instance_type_code", instanceTypeCodes)
        .findList();
  }

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
      // TODO this has issues with universes still referencing. Fix it later.
      instanceType.delete();
      instanceType = new InstanceType();
      instanceType.idKey = InstanceTypeKey.create(instanceTypeCode, providerUuid);
    }

    // Update the in-memory fields.
    instanceType.setMemSizeGB(memSize);
    instanceType.setNumCores(numCores);
    instanceType.setInstanceTypeDetails(instanceTypeDetails);
    validateInstanceType(providerUuid, instanceType);
    instanceType.save();
    return instanceType;
  }

  /**
   * Reset the 'instance_type_details_json' of all rows belonging to a specific provider in this
   * table.
   */
  public static void resetInstanceTypeDetailsForProvider(
      Provider provider, RuntimeConfGetter confGetter, boolean allowUnsupported) {
    // We do not want to reset the details for manually added instance types.

    List<InstanceType> instanceTypes = findByProvider(provider, confGetter, allowUnsupported);
    instanceTypes =
        instanceTypes.stream()
            .filter(
                supportedInstanceTypes(
                    getAWSInstancePrefixesSupported(provider, confGetter), allowUnsupported))
            .collect(Collectors.toList());

    for (InstanceType instanceType : instanceTypes) {
      instanceType.setInstanceTypeDetails(new InstanceTypeDetails());
      instanceType.save();
    }
  }

  /** Delete Instance Types corresponding to given provider */
  public static void deleteInstanceTypesForProvider(
      Provider provider, RuntimeConfGetter confGetter) {
    for (InstanceType instanceType : findByProvider(provider, confGetter, true)) {
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
      Provider provider,
      List<InstanceType> entries,
      RuntimeConfGetter confGetter,
      boolean allowUnsupported) {
    // For AWS, we would filter and show only supported instance prefixes
    entries =
        entries.stream()
            .filter(
                supportedInstanceTypes(
                    getAWSInstancePrefixesSupported(provider, confGetter), allowUnsupported))
            .collect(Collectors.toList());
    for (InstanceType instanceType : entries) {
      if (instanceType.getInstanceTypeDetails() == null) {
        instanceType.setInstanceTypeDetails(new InstanceTypeDetails());
      }
      if (instanceType.getInstanceTypeDetails().volumeDetailsList.isEmpty()) {
        instanceType
            .getInstanceTypeDetails()
            .setVolumeDetailsList(
                confGetter.getStaticConf().getInt(YB_AWS_DEFAULT_VOLUME_COUNT_KEY),
                confGetter.getStaticConf().getInt(YB_AWS_DEFAULT_VOLUME_SIZE_GB_KEY),
                VolumeType.EBS);
      }
    }
    return entries;
  }

  /** Query Helper to find supported instance types for a given cloud provider. */
  public static List<InstanceType> findByProvider(Provider provider, RuntimeConfGetter confGetter) {
    return findByProvider(provider, confGetter, false);
  }

  /** Query Helper to find supported instance types for a given cloud provider. */
  public static List<InstanceType> findByProvider(
      Provider provider, RuntimeConfGetter confGetter, boolean allowUnsupported) {
    List<InstanceType> entries =
        InstanceType.find
            .query()
            .where()
            .eq("provider_uuid", provider.getUuid())
            .eq("active", true)
            .findList();
    if (provider.getCode().equals("aws")) {
      return populateDefaultsIfEmpty(provider, entries, confGetter, allowUnsupported);
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

  public static List<String> getAWSInstancePrefixesSupported(
      Provider provider, RuntimeConfGetter confGetter) {
    UUID customerUUID = provider.getCustomerUUID();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    if (confGetter.getConfForScope(customer, CustomerConfKeys.cloudEnabled)) {
      return CLOUD_AWS_INSTANCE_PREFIXES_SUPPORTED;
    }
    return Stream.concat(
            AWS_INSTANCE_PREFIXES_SUPPORTED.stream(),
            GRAVITON_AWS_INSTANCE_PREFIXES_SUPPORTED.stream())
        .collect(Collectors.toList());
  }

  public static List<InstanceType> getInstanceTypesWithoutArch(UUID providerUuid) {
    return InstanceType.find
        .query()
        .where()
        .eq("provider_uuid", providerUuid)
        .or()
        .isNull("instance_type_details") // Check if instance_type_details is null
        .jsonNotExists(
            "instance_type_details",
            "arch") // Check if "arch" key is not present in instance_type_details JSON
        .endOr()
        .findList();
  }

  public static boolean isAzureWithLocalDisk(String instanceTypeCode) {
    return AZU_NO_LOCAL_DISK.matcher(instanceTypeCode).matches();
  }

  @JsonIgnore
  public boolean isAzureWithLocalDisk() {
    if (isCloudInstanceType()) {
      return isAzureWithLocalDisk(getInstanceTypeCode());
    }
    return isAzureWithLocalDisk(getInstanceTypeDetails().cloudInstanceTypeCodes.get(0));
  }

  @JsonIgnore
  public boolean isCloudInstanceType() {
    // There should not be any translation to cloud instance types.
    return CollectionUtils.isEmpty(getInstanceTypeDetails().cloudInstanceTypeCodes);
  }

  public static boolean isCloudInstanceType(UUID providerUuid, String instanceTypeCode) {
    return InstanceType.getOrBadRequest(providerUuid, instanceTypeCode).isCloudInstanceType();
  }

  private static boolean compareVolumeDetailsList(
      List<VolumeDetails> volDetails1, List<VolumeDetails> volDetails2) {
    if (CollectionUtils.isEmpty(volDetails1)) {
      return CollectionUtils.isEmpty(volDetails2);
    }
    if (CollectionUtils.isEmpty(volDetails2)) {
      return CollectionUtils.isEmpty(volDetails1);
    }
    if (volDetails1.size() != volDetails2.size()) {
      return false;
    }
    Comparator<VolumeDetails> comparator =
        (v1, v2) -> {
          int res = Integer.compare(v1.volumeType.ordinal(), v2.volumeType.ordinal());
          if (res != 0) {
            return res;
          }
          res = Integer.compare(v1.volumeSizeGB, v2.volumeSizeGB);
          if (res != 0) {
            return res;
          }
          return StringUtils.compare(v1.mountPath, v2.mountPath);
        };
    Collections.sort(volDetails1, comparator);
    Collections.sort(volDetails2, comparator);
    for (int idx = 0; idx < volDetails1.size(); idx++) {
      if (!volDetails1.get(idx).equals(volDetails2.get(idx))) {
        return false;
      }
    }
    return true;
  }

  public static void validateInstanceType(UUID providerUuid, InstanceType instanceType) {
    if (instanceType == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid instance type");
    }
    InstanceTypeDetails instanceTypeDetails = instanceType.getInstanceTypeDetails();
    if (instanceTypeDetails != null
        && CollectionUtils.isNotEmpty(instanceTypeDetails.cloudInstanceTypeCodes)) {
      Provider provider = Provider.getOrBadRequest(providerUuid);
      if (provider.getCloudCode() != CloudType.azu) {
        String errMsg =
            String.format(
                "Instance type code list is not supported on %s", provider.getCloudCode());
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
      Set<String> uniqueSubInstanceTypeCodes = new HashSet<>();
      Set<String> invalidInstanceTypeCodes =
          instanceTypeDetails.cloudInstanceTypeCodes.stream()
              .filter(c -> !uniqueSubInstanceTypeCodes.add(c))
              .collect(Collectors.toSet());
      if (!invalidInstanceTypeCodes.isEmpty()) {
        String errMsg =
            String.format(
                "Duplicate instance type codes - %s", String.join(", ", invalidInstanceTypeCodes));
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
      if (uniqueSubInstanceTypeCodes.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Minimum of one cloud instance type must be specified");
      }
      // For Azure, instances with and without local disks cannot be mixed up.
      boolean isLocalDisk = isAzureWithLocalDisk(uniqueSubInstanceTypeCodes.iterator().next());
      if (instanceTypeDetails.cloudInstanceTypeCodes.stream()
          .anyMatch(c -> isAzureWithLocalDisk(c) != isLocalDisk)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Instance types with/without local disks cannot be mixed");
      }
      Map<String, InstanceType> instanceTypeMap =
          InstanceType.getInstanceTypes(providerUuid, uniqueSubInstanceTypeCodes).stream()
              .filter(InstanceType::isActive)
              .collect(Collectors.toMap(InstanceType::getInstanceTypeCode, Function.identity()));
      if (instanceTypeMap.size() != uniqueSubInstanceTypeCodes.size()) {
        // Some instance type codes are not known.
        invalidInstanceTypeCodes =
            Sets.difference(uniqueSubInstanceTypeCodes, instanceTypeMap.keySet());
        String errMsg =
            String.format(
                "Non-existing instance type codes - %s",
                String.join(", ", invalidInstanceTypeCodes));
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
      // Instances must not have cloud instance type codes and must have the same number of CPU
      // cores, memory size and volume details.
      invalidInstanceTypeCodes =
          instanceTypeMap.values().stream()
              .filter(
                  n ->
                      CollectionUtils.isNotEmpty(n.getInstanceTypeDetails().cloudInstanceTypeCodes)
                          || Math.round(n.getNumCores()) != Math.round(instanceType.getNumCores())
                          || Math.round(n.getMemSizeGB()) != Math.round(instanceType.getMemSizeGB())
                          || !compareVolumeDetailsList(
                              n.getInstanceTypeDetails().volumeDetailsList,
                              instanceType.getInstanceTypeDetails().volumeDetailsList))
              .map(
                  n ->
                      String.format(
                          "%s(cores=%d, memory=%dGB, volumeDetails=%s)",
                          n.getInstanceTypeCode(),
                          Math.round(n.getNumCores()),
                          Math.round(n.getMemSizeGB()),
                          n.getInstanceTypeDetails().volumeDetailsList))
              .collect(Collectors.toSet());
      if (!invalidInstanceTypeCodes.isEmpty()) {
        String errMsg =
            String.format(
                "Invalid instance type codes - %s", String.join(", ", invalidInstanceTypeCodes));
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
    }
  }

  /** Default details for volumes attached to this instance. */
  @EqualsAndHashCode
  @ToString
  public static class VolumeDetails {
    public Integer volumeSizeGB;
    public VolumeType volumeType;
    public String mountPath;
  }

  public static class InstanceTypeDetails {
    public static final int DEFAULT_VOLUME_COUNT = 1;
    public static final int DEFAULT_GCP_VOLUME_SIZE_GB = 375;
    public static final int DEFAULT_AZU_VOLUME_SIZE_GB = 250;

    // These instance type codes are typically the ones provided by the cloud vendor.
    @ApiModelProperty(
        hidden = true,
        required = false,
        value = "Decreasing priority list of cloud or vendor instance type code")
    @JsonInclude(Include.NON_EMPTY)
    public List<String> cloudInstanceTypeCodes = new LinkedList<>();

    @ApiModelProperty(value = "Volume Details for the instance.")
    public List<VolumeDetails> volumeDetailsList = new LinkedList<>();

    @ApiModelProperty(value = "Tenancy for the instance.")
    public PublicCloudConstants.Tenancy tenancy;

    @ApiModelProperty(value = "Architecture for the instance.")
    public Architecture arch;

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
