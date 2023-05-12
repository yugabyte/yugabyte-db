package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.provider.region.AWSRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CloudImageBundleSetup extends CloudTaskBase {

  private CloudQueryHelper cloudQueryHelper;

  @Inject
  public CloudImageBundleSetup(
      BaseTaskDependencies baseTaskDependencies, CloudQueryHelper cloudQueryHelper) {
    super(baseTaskDependencies);
    this.cloudQueryHelper = cloudQueryHelper;
  }

  public static class Params extends CloudTaskParams {
    public List<ImageBundle> imageBundles;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public static void verifyImageBundleDetails(ImageBundleDetails details, Provider provider) {
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
  }

  @Override
  public void run() {
    Provider provider = getProvider();
    CloudType cloudType = provider.getCloudCode();
    List<Region> regions = provider.getRegions();

    List<ImageBundle> imageBundles = taskParams().imageBundles;
    if ((imageBundles == null || imageBundles.size() == 0)
        && provider.getImageBundles().size() == 0) {
      log.info("No image bundle specified for provider. Creating one...");
      Architecture arch = regions.get(0).getArchitecture();
      if (arch == null) {
        arch = Architecture.x86_64;
      }
      ImageBundleDetails details = new ImageBundleDetails();
      details.setArch(arch);
      if (cloudType.equals(CloudType.aws)) {
        Map<String, ImageBundleDetails.BundleInfo> regionsImageInfo = new HashMap<>();
        for (Region r : regions) {
          AWSRegionCloudInfo awsRegionCloudInfo = r.getDetails().getCloudInfo().getAws();
          ImageBundleDetails.BundleInfo bundleInfo = new ImageBundleDetails.BundleInfo();
          String ybImage = awsRegionCloudInfo.getYbImage();
          if (ybImage == null) {
            ybImage = cloudQueryHelper.getDefaultImage(r, arch.toString());
          }
          bundleInfo.setYbImage(ybImage);
          bundleInfo.setSshUserOverride(provider.getDetails().getSshUser());
          regionsImageInfo.put(r.getCode(), bundleInfo);
        }
        details.setRegions(regionsImageInfo);
      } else {
        Region region = regions.get(0);
        String ybImage = null;
        if (provider.getCloudCode().equals(CloudType.gcp)) {
          GCPRegionCloudInfo gcpRegionCloudInfo = region.getDetails().getCloudInfo().getGcp();
          ybImage = gcpRegionCloudInfo.getYbImage();
        } else if (provider.getCloudCode().equals(CloudType.azu)) {
          AzureRegionCloudInfo azuRegionCloudInfo = region.getDetails().getCloudInfo().getAzu();
          ybImage = azuRegionCloudInfo.getYbImage();
        }

        if (ybImage == null) {
          ybImage = cloudQueryHelper.getDefaultImage(region, arch.toString());
        }
        details.setGlobalYbImage(ybImage);
      }
      ImageBundle.create(
          provider, String.format("for_provider-%s", provider.getName()), details, true);
    } else if (imageBundles != null && imageBundles.size() > 0) {
      for (ImageBundle bundle : imageBundles) {
        ImageBundleDetails details = bundle.getDetails();
        Architecture arch = details.getArch();
        if (arch == null) {
          arch = Architecture.x86_64;
        }
        verifyImageBundleDetails(details, provider);
        if (cloudType.equals(CloudType.aws)) {
          // Region level image override exists for aws only.
          Map<String, ImageBundleDetails.BundleInfo> regionsImageInfo = details.getRegions();

          for (Region region : regions) {
            ImageBundleDetails.BundleInfo bundleInfo = regionsImageInfo.get(region.getCode());
            if (bundleInfo.getYbImage() == null) {
              String defaultImage = cloudQueryHelper.getDefaultImage(region, arch.toString());
              bundleInfo.setYbImage(defaultImage);
              bundleInfo.setSshUserOverride(cloudType.getSshUser());
            }

            regionsImageInfo.put(region.getCode(), bundleInfo);
          }
          details.setRegions(regionsImageInfo);
        } else {
          if (details.getGlobalYbImage() == null) {
            // for GCP/Azure images are independent of regions.
            String defaultImage = cloudQueryHelper.getDefaultImage(regions.get(0), arch.toString());
            details.setGlobalYbImage(defaultImage);
          }
        }
        if (bundle.getUseAsDefault()) {
          // Check for the existence of no other default image bundle for the provider.
          ImageBundle defaultImageBundle = ImageBundle.getDefaultForProvider(provider.getUuid());
          if (defaultImageBundle != null) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Provider %s already has %s as the default image bundle. Can't continue.",
                    provider.getUuid(), defaultImageBundle.getUuid()));
          }
        }
        ImageBundle.create(provider, bundle.getName(), details, bundle.getUseAsDefault());
      }
    }
  }
}
