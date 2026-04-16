package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.ManyToOne;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
public class ImageBundle extends Model {

  public static enum ImageBundleType {
    YBA_ACTIVE,
    YBA_DEPRECATED,
    CUSTOM
  };

  @Data
  @EqualsAndHashCode(callSuper = false)
  public static class Metadata {
    private ImageBundleType type;
    private String version;
  }

  @Data
  @EqualsAndHashCode(callSuper = false)
  public static class NodeProperties {
    private String machineImage;
    private String sshUser;
    private Integer sshPort;
  }

  @ApiModelProperty(value = "Image Bundle UUID", accessMode = READ_ONLY)
  @Id
  private UUID uuid;

  @ApiModelProperty(value = "Image Bundle Name")
  private String name;

  @ManyToOne
  @JsonBackReference("provider-image-bundles")
  private Provider provider;

  @ApiModelProperty(value = "Image Bundle Details")
  @DbJson
  private ImageBundleDetails details = new ImageBundleDetails();

  @ApiModelProperty(
      value = "Default Image Bundle. A provider can have two defaults, one per architecture")
  @Column(name = "is_default")
  private Boolean useAsDefault = false;

  @ApiModelProperty(value = "Metadata for imageBundle")
  @DbJson
  private Metadata metadata = new Metadata();

  @ApiModelProperty(value = "Is the ImageBundle Active")
  private Boolean active = true;

  public static final Finder<UUID, ImageBundle> find =
      new Finder<UUID, ImageBundle>(ImageBundle.class) {};

  public static ImageBundle getOrBadRequest(UUID providerUUID, UUID imageBundleUUID) {
    ImageBundle bundle = ImageBundle.get(providerUUID, imageBundleUUID);
    if (bundle == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid ImageBundle UUID: " + imageBundleUUID);
    }
    return bundle;
  }

  public static ImageBundle getOrBadRequest(UUID imageBundleUUID) {
    ImageBundle bundle = ImageBundle.get(imageBundleUUID);
    if (bundle == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid ImageBundle UUID: " + imageBundleUUID);
    }
    return bundle;
  }

  public static ImageBundle get(UUID imageBundleUUID) {
    return find.byId(imageBundleUUID);
  }

  public static ImageBundle get(UUID providerUUID, UUID imageBundleUUID) {
    return find.query().where().eq("provider_uuid", providerUUID).idEq(imageBundleUUID).findOne();
  }

  public static List<ImageBundle> getAll(UUID providerUUID) {
    return find.query().where().eq("provider_uuid", providerUUID).findList();
  }

  public static ImageBundle create(
      Provider provider, String name, ImageBundleDetails details, boolean isDefault) {
    return create(provider, name, details, null, isDefault);
  }

  public static ImageBundle create(
      Provider provider,
      String name,
      ImageBundleDetails details,
      Metadata metadata,
      boolean isDefault) {
    ImageBundle bundle = new ImageBundle();
    bundle.setProvider(provider);
    bundle.setName(name);
    bundle.setDetails(details);
    bundle.setMetadata(metadata);
    bundle.setUseAsDefault(isDefault);
    bundle.save();
    return bundle;
  }

  public static List<ImageBundle> getDefaultForProvider(UUID providerUUID) {
    // At a given time, two defaults can exist in image bundle one for `x86` & other for `arm`.
    return find.query().where().eq("provider_uuid", providerUUID).eq("is_default", true).findList();
  }

  public static List<ImageBundle> getBundlesForArchType(UUID providerUUID, String arch) {
    if (arch.isEmpty()) {
      // List all the bundles for the provider (non-AWS provider case).
      return find.query().where().eq("provider_uuid", providerUUID).eq("active", true).findList();
    }
    // At a given time, two defaults can exist in image bundle one for `x86` & other for `arm`.
    List<ImageBundle> bundles;
    try {
      bundles =
          find.query()
              .where()
              .eq("provider_uuid", providerUUID)
              .eq("details::json->>'arch'", arch)
              .eq("active", true)
              .findList();
    } catch (Exception e) {
      // In case exception is thrown we will fallback to manual filtering, specifically for UTs
      bundles =
          find.query().where().eq("provider_uuid", providerUUID).eq("active", true).findList();
      bundles.removeIf(
          bundle -> {
            if (bundle.getDetails() != null
                && bundle.getDetails().getArch().toString().equals(arch)) {
              return false;
            }
            return true;
          });
    }

    return bundles;
  }

  public static List<ImageBundle> getYBADefaultBundles(UUID providerUUID) {
    List<ImageBundle> bundles;
    try {
      bundles =
          find.query()
              .where()
              .eq("provider_uuid", providerUUID)
              .eq("metadata::json->>'type'", ImageBundleType.YBA_ACTIVE.toString())
              .findList();
    } catch (Exception e) {
      // In case exception is thrown we will fallback to manual filtering, specifically for UTs
      bundles = find.query().where().eq("provider_uuid", providerUUID).findList();
      bundles.removeIf(
          bundle -> {
            if (bundle.getMetadata() != null
                && bundle.getMetadata().getType() != null
                && bundle
                    .getMetadata()
                    .getType()
                    .toString()
                    .equals(ImageBundleType.YBA_ACTIVE.toString())) {
              return false;
            }
            return true;
          });
    }
    return bundles;
  }

  @JsonIgnore
  public boolean isUpdateNeeded(ImageBundle bundle) {
    return !Objects.equals(this.getUseAsDefault(), bundle.getUseAsDefault())
        || !Objects.equals(this.getDetails(), bundle.getDetails())
        || (this.getDetails().getRegions() != null
            && bundle.getDetails().getRegions() != null
            && !this.getDetails().getRegions().equals(bundle.getDetails().getRegions()))
        || !this.getName().equals(bundle.getName());
  }

  @JsonIgnore
  public boolean allowUpdateDuringUniverseAssociation(ImageBundle existingBundle) {
    ImageBundleDetails existingDetails = existingBundle.getDetails();
    ImageBundleDetails details = this.getDetails();
    // We will allow fine grain edit in case the bundle is associated with
    // the universe. We will allow addition of new AMI in the newly added
    // region but will not allow any other edit.
    if (existingBundle.getProvider().getCloudCode() == CloudType.aws) {
      // Compare that AMI is not removed for any region in used bundle for AWS.
      Map<String, ImageBundleDetails.BundleInfo> infoExistingBundle = existingDetails.getRegions();
      Map<String, ImageBundleDetails.BundleInfo> info = details.getRegions();

      // We will not allow any region AMI information to be removed from the used bundle.
      boolean allMatch =
          infoExistingBundle.entrySet().stream()
              .allMatch(
                  entry -> {
                    String key = entry.getKey();
                    ImageBundleDetails.BundleInfo bundleInfo = entry.getValue();
                    return info.containsKey(key) && info.get(key).equals(bundleInfo);
                  });

      if (!allMatch) {
        return false;
      }
    }
    return details.equals(existingDetails);
  }

  @JsonIgnore
  public long getUniverseCount() {
    Provider provider = this.provider;
    if (provider == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Image Bundle needs to be associated to a provider!");
    }
    List<ImageBundle> defaultImageBundle = ImageBundle.getDefaultForProvider(provider.getUuid());
    Set<Universe> universes =
        Customer.get(provider.getCustomerUUID()).getUniversesForProvider(provider.getUuid());
    Set<Universe> universeUsingImageBundle =
        universes.stream()
            .filter(u -> checkImageBudleInCluster(u, uuid, defaultImageBundle))
            .collect(Collectors.toSet());

    return universeUsingImageBundle.stream().count();
  }

  @JsonIgnore
  public static int getImageBundleCount(UUID providerUUID) {
    return find.query().where().eq("provider_uuid", providerUUID).findCount();
  }

  @JsonIgnore
  private boolean checkImageBudleInCluster(
      Universe universe, UUID imageBundleUUID, List<ImageBundle> defaultBundles) {
    Architecture arch = universe.getUniverseDetails().arch;
    ImageBundle defaultBundle = ImageBundleUtil.getDefaultBundleForUniverse(arch, defaultBundles);
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      if (cluster.userIntent.imageBundleUUID == null
          && defaultBundle != null
          && imageBundleUUID.equals(defaultBundle.getUuid())) {
        return true;
      } else if (cluster.userIntent.imageBundleUUID != null
          && cluster.userIntent.imageBundleUUID.equals(imageBundleUUID)) {
        return true;
      }
    }
    return false;
  }
}
