package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudImageBundleSetup;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundle.ImageBundleType;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class ImageBundleUtil {

  @Inject private CloudQueryHelper cloudQueryHelper;
  @Inject private RuntimeConfGetter runtimeConfGetter;

  public ImageBundle.NodeProperties getNodePropertiesOrFail(
      UUID imageBundleUUID, String region, String cloudCode) {
    // Disable the imageBundle validation check, till the time we have image bundle
    // as first class property in provider, so that we can fallback to region.
    boolean imageBundleValidationDisabled =
        runtimeConfGetter.getStaticConf().getBoolean("yb.cloud.enabled")
            || runtimeConfGetter.getGlobalConf(GlobalConfKeys.disableImageBundleValidation);
    ImageBundle.NodeProperties properties = new ImageBundle.NodeProperties();
    ImageBundle bundle = ImageBundle.getOrBadRequest(imageBundleUUID);
    ProviderDetails providerDetails = bundle.getProvider().getDetails();
    ImageBundleDetails bundleDetails = bundle.getDetails();
    if (Common.CloudType.aws.toString().equals(cloudCode)) {
      Map<String, ImageBundleDetails.BundleInfo> regionsBundleInfo =
          bundle.getDetails().getRegions();

      if (regionsBundleInfo.containsKey(region) || imageBundleValidationDisabled) {
        ImageBundleDetails.BundleInfo bundleInfo = new ImageBundleDetails.BundleInfo();
        if (regionsBundleInfo.containsKey(region)) {
          bundleInfo = regionsBundleInfo.get(region);
          properties.setMachineImage(bundleInfo.getYbImage());
          properties.setSshPort(bundleDetails.getSshPort());
          properties.setSshUser(bundleDetails.getSshUser());
        } else if (imageBundleValidationDisabled) {
          // In case the region object is not present in the imageBundle, & we have
          // disabled imageBundleValidation, add the empty BundleInfo object for that region.
          // So as when the imageBundle becomes first level property, we will have the complete
          // imageBundle info object.
          regionsBundleInfo.put(region, bundleInfo);
          bundle.getDetails().setRegions(regionsBundleInfo);
          bundle.update();
        }
        if (properties.getMachineImage() == null) {
          if (bundle.getMetadata().getType() != ImageBundleType.CUSTOM) {
            Region r = Region.getByCode(bundle.getProvider(), region);
            // In case, AMI id is not present in the bundle - we will extract the AMI
            // from YBA's metadata for YBA managed bundles else we will fail.
            if (properties.getMachineImage() == null) {
              // In case it is still null, we will try to fetch from the regionMetadata.
              Architecture arch = r.getArchitecture();
              if (arch == null) {
                arch = Architecture.x86_64;
              }
              properties.setMachineImage(cloudQueryHelper.getDefaultImage(r, arch.toString()));
            }
          } else {
            throw new PlatformServiceException(
                INTERNAL_SERVER_ERROR,
                String.format(
                    "AMI information is missing from bundle %s for region %s",
                    bundle.getName(), region));
          }
        }
        if (properties.getSshPort() == null) {
          properties.setSshPort(providerDetails.getSshPort());
        }
        if (properties.getSshUser() == null) {
          properties.setSshUser(providerDetails.getSshUser());
        }
      } else if (!regionsBundleInfo.containsKey(region)) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Region information is missing from the image bundle.");
      }
    } else {
      properties.setMachineImage(bundle.getDetails().getGlobalYbImage());
      String sshUser = bundleDetails.getSshUser();
      if (StringUtils.isBlank(sshUser)) {
        sshUser = providerDetails.getSshUser();
      }
      Integer sshPort = bundleDetails.getSshPort();
      if (sshPort == null) {
        sshPort = providerDetails.getSshPort();
      }
      properties.setSshUser(sshUser);
      properties.setSshPort(sshPort);
    }

    return properties;
  }

  public void updateImageBundleIfRequired(
      Provider provider, List<Region> regions, ImageBundle bundle) {
    Map<String, Region> regionsImageMap =
        regions.stream().collect(Collectors.toMap(r -> r.getCode(), r -> r));
    if (runtimeConfGetter.getGlobalConf(GlobalConfKeys.disableImageBundleValidation)) {
      // in case this is disabled, that means we still allow imageAMI edit on the region
      // level itself. Need to ensure, that region AMI ID is in sync with imageBundle.
      if (provider.getCloudCode() == CloudType.aws) {
        Map<String, ImageBundleDetails.BundleInfo> bundleInfo = bundle.getDetails().getRegions();
        regionsImageMap.forEach(
            (code, region) -> {
              String ybImage = region.getYbImage();
              ImageBundleDetails.BundleInfo info = bundleInfo.get(code);
              if (info == null) {
                info = new ImageBundleDetails.BundleInfo();
                bundleInfo.put(code, info);
              }
              if (ybImage != null) {
                info.setYbImage(ybImage);
              }
              bundle.getDetails().setRegions(bundleInfo);
              bundle.update();
            });
      } else {
        String ybImage = null;
        if (provider.getCloudCode().equals(CloudType.gcp)) {
          GCPRegionCloudInfo gcpRegionCloudInfo =
              regions.get(0).getDetails().getCloudInfo().getGcp();
          ybImage = gcpRegionCloudInfo.getYbImage();
        } else if (provider.getCloudCode().equals(CloudType.azu)) {
          AzureRegionCloudInfo azuRegionCloudInfo =
              regions.get(0).getDetails().getCloudInfo().getAzu();
          ybImage = azuRegionCloudInfo.getYbImage();
        }

        if (ybImage != null) {
          bundle.getDetails().setGlobalYbImage(ybImage);
          bundle.update();
        }
      }
    }
  }

  /*
   * Returns the default image bundle associated with the universe. In case, the universe
   * has `arch` specified as top level property we will filter the default based on the
   * achitecture, else we will return the first one from the list of default bundles.
   */
  public static ImageBundle getDefaultBundleForUniverse(
      Architecture arch, List<ImageBundle> defaultBundles) {
    ImageBundle defaultBundle;
    if (arch == null) {
      defaultBundle = defaultBundles.get(0);
    } else {
      defaultBundle =
          defaultBundles.stream()
              .filter(bundle -> bundle.getDetails().getArch().equals(arch))
              .findFirst()
              .orElse(null);
    }

    return defaultBundle;
  }

  public void migrateImageBundlesForProviders(Provider provider) {
    boolean enableVMOSPatching = runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching);
    List<ImageBundle> bundles = provider.getImageBundles();
    if (bundles.size() == 0) {
      return;
    }
    long customBundlesCount =
        bundles.stream()
            .filter(
                iB ->
                    iB.getMetadata() != null
                        && iB.getMetadata().getType() != null
                        && iB.getMetadata().getType() == ImageBundleType.CUSTOM)
            .count();
    if (customBundlesCount == bundles.size()) {
      // We will not generate image bundles in case a provider contains
      // all explicit marked custom bundles.
      return;
    }

    // Retrive the currentYbaDefaultImageBundles, so that we can mark them YBA_DEPRECATED.
    boolean x86YBADefaultBundleMarkedDefault = false;
    boolean aarch64YBADefaultBundleMarkedDefault = false;
    List<ImageBundle> getYbaDefaultImageBundles =
        ImageBundle.getYBADefaultBundles(provider.getUuid());
    if (getYbaDefaultImageBundles.size() == 0) {
      // These will be the bundles created before migration & does not contain the metadata.
      List<ImageBundle> providerBundles = provider.getImageBundles();
      x86YBADefaultBundleMarkedDefault =
          provider.getImageBundles().stream()
              .noneMatch(
                  bundle ->
                      bundle.getDetails().getArch() == Architecture.x86_64
                          && bundle.getUseAsDefault());

      aarch64YBADefaultBundleMarkedDefault =
          provider.getImageBundles().stream()
              .noneMatch(
                  bundle ->
                      bundle.getDetails().getArch() == Architecture.aarch64
                          && bundle.getUseAsDefault());
    } else {
      for (ImageBundle ybaDefaultBundle : getYbaDefaultImageBundles) {
        if (ybaDefaultBundle.getDetails() == null) {
          continue;
        }
        boolean isMarkedDefault = ybaDefaultBundle.getUseAsDefault().booleanValue();
        Architecture bundleArch = ybaDefaultBundle.getDetails().getArch();
        if (bundleArch == Architecture.aarch64 && isMarkedDefault) {
          aarch64YBADefaultBundleMarkedDefault = true;
        }
        if (bundleArch == Architecture.x86_64 && isMarkedDefault) {
          x86YBADefaultBundleMarkedDefault = true;
        }

        if (ybaDefaultBundle.getMetadata() != null
            && ybaDefaultBundle.getMetadata().getType() != null) {
          ybaDefaultBundle.getMetadata().setType(ImageBundleType.YBA_DEPRECATED);
          if (isMarkedDefault) {
            ybaDefaultBundle.setUseAsDefault(false);
          }
          ybaDefaultBundle.update();
        }
      }
    }

    // Populate the new YBA_ACTIVE bundle for x86 arch.
    CloudImageBundleSetup.generateYBADefaultImageBundle(
        provider,
        cloudQueryHelper,
        Architecture.x86_64,
        x86YBADefaultBundleMarkedDefault,
        true,
        enableVMOSPatching);
    // Populate the new YBA_ACTIVE bundle for aarch64 arch.
    CloudImageBundleSetup.generateYBADefaultImageBundle(
        provider,
        cloudQueryHelper,
        Architecture.aarch64,
        aarch64YBADefaultBundleMarkedDefault,
        true,
        enableVMOSPatching);
  }

  public boolean migrateYBADefaultBundles(
      Map<String, String> currOSVersionDBMap, Provider provider) {
    String providerCode = provider.getCode();
    if (currOSVersionDBMap != null
        && currOSVersionDBMap.containsKey("version")
        && !currOSVersionDBMap
            .get("version")
            .equals(CloudImageBundleSetup.CLOUD_OS_MAP.get(providerCode).getVersion())) {
      return true;
    }

    List<ImageBundle> getYbaDefaultImageBundles =
        ImageBundle.getYBADefaultBundles(provider.getUuid());
    if (getYbaDefaultImageBundles.size() != 0) {
      ImageBundle ybaDefaultBundle = getYbaDefaultImageBundles.get(0);
      if (ybaDefaultBundle.getMetadata() == null
          || (ybaDefaultBundle.getMetadata() != null
              && ybaDefaultBundle.getMetadata().getVersion() != null
              && !(ybaDefaultBundle.getMetadata().getVersion())
                  .equals(CloudImageBundleSetup.CLOUD_OS_MAP.get(providerCode).getVersion()))) {
        return true;
      }
    } else if (getYbaDefaultImageBundles.size() == 0) {
      return true;
    }

    return false;
  }

  public Map<UUID, ImageBundle> collectUniversesImageBundles() {
    Map<UUID, ImageBundle> imageBundleMap = new HashMap<>();
    for (Customer customer : Customer.getAll()) {
      Set<Universe> universes = Universe.getAllWithoutResources(customer);

      for (Universe universe : universes) {
        // Assumption both the primary & rr cluster uses the same provider.
        UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
        if (userIntent != null) {
          CloudType cloudType = userIntent.providerType;
          if (!cloudType.imageBundleSupported()) {
            continue;
          }
          UUID imageBundleUUID = userIntent.imageBundleUUID;
          if (imageBundleUUID != null && !imageBundleMap.containsKey(imageBundleUUID)) {
            ImageBundle bundle = ImageBundle.get(imageBundleUUID);
            imageBundleMap.put(imageBundleUUID, bundle);
          }
        }
      }
    }

    return imageBundleMap;
  }

  /** Find and return the SSH user that works for the node. */
  public String findEffectiveSshUser(
      Provider provider, Universe universe, NodeDetails nodeDetails) {
    ProviderDetails providerDetails = provider.getDetails();
    String sshUser = providerDetails.getSshUser();
    if (StringUtils.isBlank(sshUser)) {
      CloudType cloudType = universe.getNodeDeploymentMode(nodeDetails);
      sshUser = cloudType.getSshUser();
    }
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    UUID imageBundleUUID =
        Util.retreiveImageBundleUUID(
            universe.getUniverseDetails().arch, cluster.userIntent, provider);
    if (imageBundleUUID != null) {
      ImageBundle.NodeProperties toOverwriteNodeProperties =
          getNodePropertiesOrFail(
              imageBundleUUID,
              nodeDetails.cloudInfo.region,
              cluster.userIntent.providerType.toString());
      sshUser = toOverwriteNodeProperties.getSshUser();
    }
    return sshUser;
  }
}
