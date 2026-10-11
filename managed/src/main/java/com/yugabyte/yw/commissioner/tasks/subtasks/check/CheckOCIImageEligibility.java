// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.oracle.bmc.core.model.Image;
import com.yugabyte.yw.cloud.oci.OCICloudImpl;
import com.yugabyte.yw.cloud.oci.OCICloudUtil;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Provider;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

/**
 * Precheck for an OCI VM image upgrade. OCI rejects a boot volume replacement onto a Marketplace
 * image, so each target image is looked up live and the upgrade fails before any node is taken
 * down.
 */
public class CheckOCIImageEligibility extends UniverseTaskBase {

  private final OCICloudImpl ociCloudImpl;

  @Inject
  protected CheckOCIImageEligibility(
      BaseTaskDependencies baseTaskDependencies, OCICloudImpl ociCloudImpl) {
    super(baseTaskDependencies);
    this.ociCloudImpl = ociCloudImpl;
  }

  @Data
  @NoArgsConstructor
  @AllArgsConstructor
  public static class TargetImage {
    private UUID providerUuid;
    private String regionCode;
    private String imageId;
  }

  public static class Params extends UniverseTaskParams {
    public List<TargetImage> targetImages = new ArrayList<>();
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    // TODO(PLAT-22730): use the Marketplace flag stored on the image bundle (PLAT-22702) instead
    // of a live lookup.
    for (TargetImage target : taskParams().targetImages) {
      Provider provider = Provider.getOrBadRequest(target.getProviderUuid());
      Image image =
          ociCloudImpl.getImageOrBadRequest(provider, target.getRegionCode(), target.getImageId());
      if (OCICloudUtil.getImageType(image) == OCICloudUtil.ImageType.MARKETPLACE) {
        // TODO(PLAT-22701): point to the Marketplace image upgrade here once it exists.
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Target image %s in region %s is an OCI Marketplace image. OCI does not support"
                    + " replacing a boot volume with a Marketplace image; use a custom or Oracle"
                    + " platform image instead.",
                target.getImageId(), target.getRegionCode()));
      }
    }
  }
}
