// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails.BundleInfo;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Precheck for an OCI VM image upgrade. OCI rejects a boot volume replacement onto a Marketplace
 * image, and Oracle does not support one on an instance running a Marketplace image. Custom images
 * built from a Marketplace image may not be supported either, depending on publisher licensing and
 * image properties. So the upgrade fails before any node is taken down unless the image bundles
 * record both the target images and the images the nodes run now as not Marketplace-based, or the
 * universe level runtime config is set to skip this check. The UI reads the same bundle field, so a
 * missing value is looked up and stored in the bundle rather than only used here. Universes on
 * Marketplace-based images change images through a full move instead: editing the universe's image
 * bundle replaces every node.
 */
@Slf4j
public class CheckOCIImageEligibility extends UniverseTaskBase {

  private final ImageBundleUtil imageBundleUtil;

  @Inject
  protected CheckOCIImageEligibility(
      BaseTaskDependencies baseTaskDependencies, ImageBundleUtil imageBundleUtil) {
    super(baseTaskDependencies);
    this.imageBundleUtil = imageBundleUtil;
  }

  @Data
  @NoArgsConstructor
  @AllArgsConstructor
  public static class BundleImage {
    private UUID imageBundleUuid;
    private String regionCode;
  }

  public static class Params extends UniverseTaskParams {
    public List<BundleImage> targetImages = new ArrayList<>();
    public List<BundleImage> currentImages = new ArrayList<>();
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    if (confGetter.getConfForScope(
        getUniverse(), UniverseConfKeys.ociAllowBootVolumeReplaceForMarketplaceBasedImages)) {
      return;
    }
    for (BundleImage image : taskParams().targetImages) {
      checkNotMarketplaceBased(image, "Target");
    }
    for (BundleImage image : taskParams().currentImages) {
      checkNotMarketplaceBased(image, "Current");
    }
  }

  private void checkNotMarketplaceBased(BundleImage image, String kind) {
    String configKey = UniverseConfKeys.ociAllowBootVolumeReplaceForMarketplaceBasedImages.getKey();
    String regionCode = image.getRegionCode();
    ImageBundle bundle = ImageBundle.get(image.getImageBundleUuid());
    BundleInfo info = getRegionInfo(bundle, regionCode);
    String imageId = info == null ? null : info.getYbImage();
    Boolean marketplaceBased = info == null ? null : info.getIsImageMarketplaceBased();
    if (info != null && marketplaceBased == null) {
      marketplaceBased = lookUpAndStore(bundle, regionCode, imageId);
    }
    if (marketplaceBased == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "YBA could not determine whether %s image %s in region %s is an OCI Marketplace image"
                  + " or a custom image built from one. Set runtime config %s to true to skip this"
                  + " check.",
              kind.toLowerCase(),
              info == null ? "(no image bundle entry)" : imageId,
              regionCode,
              configKey));
    }
    if (marketplaceBased) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s image %s in region %s is an OCI Marketplace image or a custom image built from"
                  + " one, which OCI boot volume replacement does not support. To move the"
                  + " universe to a new image, change its image bundle by editing the universe"
                  + " instead, which replaces every node with a new one. Set runtime config %s to"
                  + " true if this is wrong.",
              kind, imageId, regionCode, configKey));
    }
  }

  @Nullable
  private Boolean lookUpAndStore(ImageBundle bundle, String regionCode, String imageId) {
    Boolean marketplaceBased;
    try {
      marketplaceBased =
          imageBundleUtil.lookUpImageMarketplaceBased(bundle.getProvider(), regionCode, imageId);
    } catch (RuntimeException e) {
      log.warn(
          "Could not look up whether OCI image {} in region {} is Marketplace-based",
          imageId,
          regionCode,
          e);
      return null;
    }
    if (marketplaceBased == null) {
      return null;
    }
    // Re-read so that a bundle edit made since the first read is not overwritten.
    ImageBundle latest = ImageBundle.get(bundle.getUuid());
    BundleInfo info = getRegionInfo(latest, regionCode);
    if (info != null
        && imageId.equals(info.getYbImage())
        && info.getIsImageMarketplaceBased() == null) {
      info.setIsImageMarketplaceBased(marketplaceBased);
      latest.setDetails(latest.getDetails());
      latest.update();
    }
    return marketplaceBased;
  }

  @Nullable
  private static BundleInfo getRegionInfo(@Nullable ImageBundle bundle, String regionCode) {
    if (bundle == null || bundle.getDetails() == null || bundle.getDetails().getRegions() == null) {
      return null;
    }
    BundleInfo info = bundle.getDetails().getRegions().get(regionCode);
    return info == null || StringUtils.isBlank(info.getYbImage()) ? null : info;
  }
}
