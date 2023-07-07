package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ImageBundleUtil {

  @Inject private CloudQueryHelper cloudQueryHelper;
  @Inject private RuntimeConfGetter runtimeConfGetter;

  public ImageBundle.NodeProperties getNodePropertiesOrFail(
      UUID imageBundleUUID, String region, String cloudCode) {
    boolean isCloudEnabled = runtimeConfGetter.getStaticConf().getBoolean("yb.cloud.enabled");
    ImageBundle.NodeProperties properties = new ImageBundle.NodeProperties();
    ImageBundle bundle = ImageBundle.getOrBadRequest(imageBundleUUID);
    ProviderDetails providerDetails = bundle.getProvider().getDetails();
    if (Common.CloudType.aws.toString().equals(cloudCode)) {
      Map<String, ImageBundleDetails.BundleInfo> regionsBundleInfo =
          bundle.getDetails().getRegions();

      if (regionsBundleInfo.containsKey(region) || isCloudEnabled) {
        ImageBundleDetails.BundleInfo bundleInfo = new ImageBundleDetails.BundleInfo();
        if (regionsBundleInfo.containsKey(region)) {
          bundleInfo = regionsBundleInfo.get(region);
          properties.setMachineImage(bundleInfo.getYbImage());
          properties.setSshPort(bundleInfo.getSshPortOverride());
          properties.setSshUser(bundleInfo.getSshUserOverride());
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
}
