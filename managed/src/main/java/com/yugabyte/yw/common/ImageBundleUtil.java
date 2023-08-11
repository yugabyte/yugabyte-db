package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

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
    if (Common.CloudType.aws.toString().equals(cloudCode)) {
      Map<String, ImageBundleDetails.BundleInfo> regionsBundleInfo =
          bundle.getDetails().getRegions();

      if (regionsBundleInfo.containsKey(region) || imageBundleValidationDisabled) {
        ImageBundleDetails.BundleInfo bundleInfo = new ImageBundleDetails.BundleInfo();
        if (regionsBundleInfo.containsKey(region)) {
          bundleInfo = regionsBundleInfo.get(region);
          properties.setMachineImage(bundleInfo.getYbImage());
          properties.setSshPort(bundleInfo.getSshPortOverride());
          properties.setSshUser(bundleInfo.getSshUserOverride());
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
          Region r = Region.getByCode(bundle.getProvider(), region);
          properties.setMachineImage(r.getYbImage());
          if (properties.getMachineImage() == null) {
            // In case it is still null, we will try to fetch from the regionMetadata.
            Architecture arch = r.getArchitecture();
            if (arch == null) {
              arch = Architecture.x86_64;
            }
            properties.setMachineImage(cloudQueryHelper.getDefaultImage(r, arch.toString()));
          }
        }
        if (properties.getSshPort() == null) {
          properties.setSshPort(providerDetails.getSshPort());
        }
        if (properties.getSshUser() == null) {
          properties.setSshUser(providerDetails.getSshUser());
        }
      } else {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Region information is missing from the image bundle.");
      }
    } else {
      properties.setMachineImage(bundle.getDetails().getGlobalYbImage());
      properties.setSshUser(providerDetails.getSshUser());
      properties.setSshPort(providerDetails.getSshPort());
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
                info.setSshUserOverride(provider.getDetails().getSshUser());
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
}
