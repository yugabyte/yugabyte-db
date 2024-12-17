package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.ImageBundleHandler;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundle.ImageBundleType;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.ImageBundleDetails.BundleInfo;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.provider.region.AWSRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.BooleanUtils;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class CloudImageBundleSetup extends CloudTaskBase {

  @Data
  @AllArgsConstructor
  public static class CloudOS {
    private String version;
    private String name;
  }

  private CloudQueryHelper cloudQueryHelper;
  private RuntimeConfGetter confGetter;
  private ImageBundleHandler imageBundleHandler;
  private ImageBundleUtil imageBundleUtil;
  public static final Map<String, CloudOS> CLOUD_OS_MAP =
      ImmutableMap.of(
          "aws", new CloudOS("8.9_20240303", "AlmaLinux"),
          "gcp", new CloudOS("8.9_v20240515", "AlmaLinux"),
          "azu", new CloudOS("8.9_2023112501", "AlmaLinux"));

  @Inject
  public CloudImageBundleSetup(
      BaseTaskDependencies baseTaskDependencies,
      CloudQueryHelper cloudQueryHelper,
      RuntimeConfGetter confGetter,
      ImageBundleUtil imageBundleUtil,
      ImageBundleHandler imageBundleHandler) {
    super(baseTaskDependencies);
    this.cloudQueryHelper = cloudQueryHelper;
    this.confGetter = confGetter;
    this.imageBundleHandler = imageBundleHandler;
    this.imageBundleUtil = imageBundleUtil;
  }

  public static class Params extends CloudTaskParams {
    public List<ImageBundle> imageBundles;
    public boolean updateBundleRequest = false;
    public boolean isFirstTry = true;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public static void verifyImageBundleDetails(ImageBundleDetails details, Provider provider) {
    if (provider.getCloudCode() != CloudType.aws) {
      return;
    }

    List<Region> regions = provider.getRegions();
    Map<String, ImageBundleDetails.BundleInfo> regionsImageInfo = details.getRegions();

    for (Region region : regions) {
      // Added region is not present in the passed imageBundle.
      if (regionsImageInfo.get(region.getCode()) == null) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            String.format("Region %s is missing from the image bundle.", region.getCode()));
      }
    }

    if (provider.getCloudCode() == CloudType.aws && regionsImageInfo != null) {
      boolean allYbImagesNull = true;
      boolean allYbImagesNonNull = true;
      for (ImageBundleDetails.BundleInfo bundleInfo : regionsImageInfo.values()) {
        String ybImage = bundleInfo.getYbImage();
        allYbImagesNull &= (ybImage == null);
        allYbImagesNonNull &= (ybImage != null);

        if (!(allYbImagesNull || allYbImagesNonNull)) {
          throw new PlatformServiceException(
              BAD_REQUEST, String.format("AMI id should be specified for all regions or none."));
        }
      }
    }
  }

  public static void generateYBADefaultImageBundle(
      Provider provider,
      CloudQueryHelper cloudQueryHelper,
      Architecture arch,
      boolean isDefault,
      boolean forceFetchFromMetadata,
      boolean enableVMOSPatching) {
    List<Region> regions = provider.getRegions();
    CloudType cloudType = provider.getCloudCode();
    if (arch == null) {
      arch = Architecture.x86_64;
    }
    if (cloudType != CloudType.aws && arch == Architecture.aarch64) {
      // Need not to generate bundles for aarch type for non-AWS providers.
      return;
    }

    ImageBundleDetails details = new ImageBundleDetails();
    details.setArch(arch);
    boolean isCustomImage = false;
    if (cloudType.equals(CloudType.aws)) {
      Map<String, ImageBundleDetails.BundleInfo> regionsImageInfo = new HashMap<>();
      for (Region r : regions) {
        String ybImage = null;
        if (r.getDetails() != null && r.getDetails().getCloudInfo() != null) {
          AWSRegionCloudInfo awsRegionCloudInfo = r.getDetails().getCloudInfo().getAws();
          if (awsRegionCloudInfo != null) {
            ybImage = awsRegionCloudInfo.getYbImage();
          }
        }
        ImageBundleDetails.BundleInfo bundleInfo = new ImageBundleDetails.BundleInfo();
        if (ybImage == null || forceFetchFromMetadata) {
          ybImage = cloudQueryHelper.getDefaultImage(r, arch.toString());
        } else if (ybImage != null) {
          isCustomImage = true;
          // We are doing this as, once the bundle is configured with the custom image,
          // we should get rid of the image in the region object, so that during bundle
          // update it does not pick the image from region.
          if (enableVMOSPatching) {
            r.setYbImage(null);
            r.save();
          }
        }
        bundleInfo.setYbImage(ybImage);
        if (isCustomImage) {
          details.setSshUser(provider.getDetails().getSshUser());
        } else {
          details.setSshUser(cloudType.getSshUser());
        }
        regionsImageInfo.put(r.getCode(), bundleInfo);
      }
      details.setRegions(regionsImageInfo);
    } else {
      Region region = regions.get(0);
      String ybImage = null;
      if (region.getDetails() != null && region.getDetails().getCloudInfo() != null) {
        if (provider.getCloudCode().equals(CloudType.gcp)) {
          GCPRegionCloudInfo gcpRegionCloudInfo = region.getDetails().getCloudInfo().getGcp();
          if (gcpRegionCloudInfo != null) {
            ybImage = gcpRegionCloudInfo.getYbImage();
          }
        } else if (provider.getCloudCode().equals(CloudType.azu)) {
          AzureRegionCloudInfo azuRegionCloudInfo = region.getDetails().getCloudInfo().getAzu();
          if (azuRegionCloudInfo != null) {
            ybImage = azuRegionCloudInfo.getYbImage();
          }
        }
      }

      if (ybImage == null || forceFetchFromMetadata) {
        ybImage = cloudQueryHelper.getDefaultImage(region, arch.toString());
      } else if (ybImage != null) {
        isCustomImage = true;
        // We are doing this as, once the bundle is configured with the custom image,
        // we should get rid of the image in the region object, so that during bundle
        // update it does not pick the image from region.
        if (enableVMOSPatching) {
          regions.stream()
              .forEach(
                  r -> {
                    r.setYbImage(null);
                    r.save();
                  });
        }
      }
      details.setGlobalYbImage(ybImage);
      details.setSshUser(provider.getDetails().getSshUser());
    }
    if (provider.getDetails().getSshPort() != null) {
      details.setSshPort(provider.getDetails().getSshPort());
    } else {
      details.setSshPort(22);
    }
    // If the bundle is not specified we will create YBA default with the type
    // YBA_ACTIVE.
    ImageBundle.Metadata metadata = new ImageBundle.Metadata();
    String bundleName = "";
    if (isCustomImage) {
      metadata.setType(ImageBundleType.CUSTOM);
      bundleName = provider.getName() + "_Custom_Bundle";
    } else {
      metadata.setType(ImageBundleType.YBA_ACTIVE);
      metadata.setVersion(CLOUD_OS_MAP.get(provider.getCode()).getVersion());
      bundleName = getDefaultImageBundleName(provider.getCode());
    }
    ImageBundle.create(provider, bundleName, details, metadata, isDefault);
  }

  @Override
  public void run() {
    Provider provider = getProvider(true);
    List<Region> regions = provider.getRegions();

    List<ImageBundle> imageBundles = taskParams().imageBundles;
    boolean enableVMOSPatching = confGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching);
    if ((imageBundles == null || imageBundles.size() == 0)
        && provider.getImageBundles().size() == 0
        && !taskParams().updateBundleRequest) {
      log.info("No image bundle specified for provider. Creating one...");
      Architecture arch = regions.get(0).getArchitecture();
      if (arch == null && provider.getCloudCode() == CloudType.aws) {
        Optional<Region> regionWithImage =
            regions.stream().filter(r -> r.getYbImage() != null).findFirst();
        if (regionWithImage.isPresent()) {
          String architecture = cloudQueryHelper.getImageArchitecture(regionWithImage.get());
          try {
            if (StringUtils.isNotBlank(architecture)) {
              // exlicitly overriding arch name to maintain equivalent type of architecture.
              if (architecture.equals("arm64")) {
                architecture = Architecture.aarch64.name();
              }
              arch = Architecture.valueOf(architecture);
            }
          } catch (IllegalArgumentException e) {
            log.warn("{} not a valid architecture", architecture);
          }
        }
      }
      generateYBADefaultImageBundle(
          provider, cloudQueryHelper, arch, true, false, enableVMOSPatching);
    } else if (imageBundles != null) {
      Map<UUID, ImageBundle> existingImageBundles =
          provider.getImageBundles().stream()
              .collect(Collectors.toMap(iB -> iB.getUuid(), iB -> iB));
      if (!taskParams().isFirstTry
          && existingImageBundles != null
          && existingImageBundles.size() > 0) {
        // In case the provider creation task is retried & imageBundle
        // creation failed in first try mid-way, we will delete up existing Bundles
        existingImageBundles.forEach((bundleUUID, bundle) -> bundle.delete());
      }
      for (ImageBundle bundle : imageBundles) {
        if (taskParams().updateBundleRequest && bundle.getUuid() != null) {
          updateBundles(
              provider,
              regions,
              bundle,
              existingImageBundles.get(bundle.getUuid()),
              enableVMOSPatching);
          existingImageBundles.remove(bundle.getUuid());
        } else {
          createBundle(provider, regions, bundle);
        }
      }

      if (taskParams().updateBundleRequest) {
        existingImageBundles.forEach(
            (uuid, bundle) -> {
              imageBundleHandler.doDelete(provider.getUuid(), bundle.getUuid());
            });
      }
    }
    // to be removed when we remove support for useIMDSv2 at provider level
    if (provider.getCloudCode().equals(CloudType.aws)
        && provider.getDetails().getCloudInfo() != null) {
      if (BooleanUtils.isTrue(provider.getDetails().getCloudInfo().getAws().useIMDSv2)) {
        for (ImageBundle bundle : ImageBundle.getAll(provider.getUuid())) {
          ImageBundleDetails details = bundle.getDetails();
          details.setUseIMDSv2(true);
          bundle.setDetails(details);
          bundle.save();
        }
      }
    }
  }

  private void updateBundles(
      Provider provider,
      List<Region> regions,
      ImageBundle bundle,
      ImageBundle existingBundle,
      boolean enableVMOSPatching) {
    bundle.setProvider(provider);
    if (bundle.isUpdateNeeded(existingBundle)) {
      imageBundleHandler.doEdit(provider, bundle.getUuid(), bundle, true);
    }

    if (enableVMOSPatching
        && provider.getCloudCode() == CloudType.aws
        && bundle.getMetadata() != null
        && bundle.getMetadata().getType() == ImageBundleType.YBA_ACTIVE) {
      // In case the region is added as part of provider edit, we will add
      // it's reference in YBA_ACTIVE image_bundle.
      updateYBAActiveImageBundles(provider, bundle, provider.getRegions());
    }
    imageBundleUtil.updateImageBundleIfRequired(provider, regions, bundle);
  }

  private void updateYBAActiveImageBundles(
      Provider provider, ImageBundle bundle, List<Region> regions) {
    ImageBundleDetails details = bundle.getDetails();
    CloudType cloudType = provider.getCloudCode();
    if (details == null) {
      log.error(
          String.format("Image Bundle %s is missing details. Can't continue", bundle.getName()));
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Image Bundle %s is missing details. Can't continue", bundle.getName()));
    }
    for (Region region : regions) {
      Map<String, BundleInfo> regionBundleInfo = details.getRegions();
      BundleInfo info = regionBundleInfo.getOrDefault(region.getCode(), new BundleInfo());
      if (info != null && StringUtils.isNotEmpty(info.getYbImage())) {
        continue;
      }
      String defaultRegionImage =
          cloudQueryHelper.getDefaultImage(region, bundle.getDetails().getArch().toString());
      info.setYbImage(defaultRegionImage);
      details.setSshUser(cloudType.getSshUser());

      regionBundleInfo.put(region.getCode(), info);
      details.setRegions(regionBundleInfo);
    }
    bundle.setDetails(details);
    bundle.update();
  }

  private void createBundle(Provider provider, List<Region> regions, ImageBundle bundle) {
    CloudType cloudType = provider.getCloudCode();
    ImageBundleDetails details = bundle.getDetails();
    ImageBundle.Metadata metadata = new ImageBundle.Metadata();
    final Architecture arch = details.getArch();
    if (arch == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Image Bundle must be associated with an architecture.");
    }
    verifyImageBundleDetails(details, provider);
    List<ImageBundle> ybaActiveBundles = ImageBundle.getYBADefaultBundles(provider.getUuid());
    ImageBundle ybaActiveBundle =
        ybaActiveBundles.stream()
            .filter(iB -> iB.getDetails().getArch() == arch)
            .findFirst()
            .orElse(null);
    if (cloudType.equals(CloudType.aws)) {
      // Region level image override exists for aws only.
      Map<String, ImageBundleDetails.BundleInfo> regionsImageInfo = details.getRegions();

      for (Region region : regions) {
        ImageBundleDetails.BundleInfo bundleInfo = regionsImageInfo.get(region.getCode());
        if (bundleInfo.getYbImage() == null) {
          if (ybaActiveBundle != null) {
            throw new PlatformServiceException(
                BAD_REQUEST, "YBA_ACTIVE bundle is already associated with the provider");
          }
          // Set the name as per the current release version.
          bundle.setName(getDefaultImageBundleName(provider.getCode()));
          String defaultImage = cloudQueryHelper.getDefaultImage(region, arch.toString());
          bundleInfo.setYbImage(defaultImage);
          details.setSshUser(cloudType.getSshUser());
          details.setSshPort(22);
          // If we are populating the ybImage, bundle will be YBA_DEFAULT.
          metadata.setType(ImageBundleType.YBA_ACTIVE);
          metadata.setVersion(CLOUD_OS_MAP.get(provider.getCode()).getVersion());
        } else {
          // In case user specified the AMI ids bundle will be CUSTOM.
          if (StringUtils.isNotBlank(bundleInfo.getSshUserOverride())) {
            details.setSshUser(bundleInfo.getSshUserOverride());
            bundleInfo.setSshUserOverride(null);
          }
          if (bundleInfo.getSshPortOverride() != null) {
            details.setSshPort(bundleInfo.getSshPortOverride());
            bundleInfo.setSshPortOverride(null);
          }
          metadata.setType(ImageBundleType.CUSTOM);
        }

        regionsImageInfo.put(region.getCode(), bundleInfo);
      }
      details.setRegions(regionsImageInfo);
    } else {
      if (details.getGlobalYbImage() == null) {
        if (ybaActiveBundle != null) {
          throw new PlatformServiceException(
              BAD_REQUEST, "YBA_ACTIVE bundle is already associated with the provider");
        }
        bundle.setName(getDefaultImageBundleName(provider.getCode()));
        // for GCP/Azure images are independent of regions.
        String defaultImage = cloudQueryHelper.getDefaultImage(regions.get(0), arch.toString());
        details.setGlobalYbImage(defaultImage);
        // In case ybImage is not specified the bundle type will be YBA_DEFAULT.
        metadata.setType(ImageBundleType.YBA_ACTIVE);
        metadata.setVersion(CLOUD_OS_MAP.get(provider.getCode()).getVersion());
      } else {
        // In case user specified the image id bundle will be CUSTOM.
        metadata.setType(ImageBundleType.CUSTOM);
      }
      if (details.getSshPort() == null) {
        details.setSshPort(22);
      }
      if (StringUtils.isBlank(details.getSshUser())) {
        details.setSshUser(cloudType.getSshUser());
      }
    }
    if (bundle.getUseAsDefault()) {
      // Check for the existence of no other default image bundle for the provider.
      List<ImageBundle> defaultImageBundles = ImageBundle.getDefaultForProvider(provider.getUuid());
      Optional<ImageBundle> defaultImageBundle =
          defaultImageBundles.stream()
              .filter(IBundle -> IBundle.getDetails().getArch().equals(arch))
              .findFirst();
      if (defaultImageBundle.isPresent()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Provider %s already has %s as the default image bundle for architecture"
                    + " type %s. Can't continue.",
                provider.getUuid(), defaultImageBundle.get().getUuid(), arch.toString()));
      }
    }
    ImageBundle.create(provider, bundle.getName(), details, metadata, bundle.getUseAsDefault());
  }

  private static String getDefaultImageBundleName(String cloudCode) {
    CloudOS cloudOS = CLOUD_OS_MAP.get(cloudCode);
    return "YBA-Managed-" + cloudOS.getName() + "-" + cloudOS.getVersion();
  }
}
