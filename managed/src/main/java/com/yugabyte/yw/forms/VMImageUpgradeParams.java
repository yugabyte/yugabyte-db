// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YBADeprecated;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = VMImageUpgradeParams.Converter.class)
public class VMImageUpgradeParams extends UpgradeTaskParams {
  public enum VmUpgradeTaskType {
    VmUpgradeWithBaseImages,
    VmUpgradeWithCustomImages,
    None
  }

  @YBADeprecated(sinceDate = "2023-03-30", sinceYBAVersion = "2.18.0")
  @ApiModelProperty(
      value =
          "Map  of region UUID to AMI name. Deprecated: sinceDate=2023-03-30,"
              + "sinceYBAVersion=2.18.0, Use imageBundle instead.",
      required = false,
      example =
          "{\n"
              + "    'b28e0813-4866-4a2d-89f3-52265766d666':"
              + " 'OpenLogic:CentOS:7_9:7.9.2022020700',\n"
              + "    'b28e0813-4866-4a2d-89f3-52265766d666':"
              + " 'ami-0f12219b4df721aa6',\n"
              + "    'd73833fc-0812-4a01-98f8-f4f24db76dbe':"
              + " 'https://www.googleapis.com/compute/v1/projects/rhel-cloud/global/images/"
              + "rhel-8-v20221102'\n"
              + "  }")
  public Map<UUID, String> machineImages = new HashMap<>();

  // Use whenwe want to use a different SSH_USER instead of what is defined in the default
  // accessKey.
  @YBADeprecated(sinceDate = "2023-03-30", sinceYBAVersion = "2.18.0")
  @ApiModelProperty(
      value =
          "Map of region UUID to SSH User override. Deprecated: sinceDate=2023-03-30,"
              + "sinceYBAVersion=2.18.0, Use imageBundle instead.",
      required = false,
      example = "{\n    'b28e0813-4866-4a2d-89f3-52265766d666': 'ec2-user',\n  }")
  public Map<UUID, String> sshUserOverrideMap = new HashMap<>();

  @ApiModelProperty("ImageBundle to be used for upgrade")
  public UUID imageBundleUUID;

  public boolean forceVMImageUpgrade = false;
  public String ybSoftwareVersion = null;

  @JsonIgnore public final Map<UUID, UUID> nodeToRegion = new HashMap<>();
  @JsonIgnore public boolean isSoftwareUpdateViaVm = false;
  @JsonIgnore public VmUpgradeTaskType vmUpgradeTaskType = VmUpgradeTaskType.None;

  public VMImageUpgradeParams() {}

  @JsonCreator
  public VMImageUpgradeParams(
      @JsonProperty(value = "machineImages", required = false) Map<UUID, String> machineImages,
      @JsonProperty(value = "imageBundleUUID", required = false) UUID imageBundleUUID) {
    this.machineImages = machineImages;
    this.imageBundleUUID = imageBundleUUID;
  }

  @Override
  // This method is not just doing verification. It is also setting instance members
  // which are used in other methods.
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);

    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Only ROLLING_UPGRADE option is supported for OS upgrades.");
    }

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    vmUpgradeTaskType =
        StringUtils.isNotBlank(ybSoftwareVersion)
            ? VmUpgradeTaskType.VmUpgradeWithCustomImages
            : VmUpgradeTaskType.VmUpgradeWithBaseImages;
    isSoftwareUpdateViaVm =
        (StringUtils.isNotBlank(ybSoftwareVersion)
            && !ybSoftwareVersion.equals(userIntent.ybSoftwareVersion));
    CloudType provider = userIntent.providerType;
    if (!(provider == CloudType.gcp || provider == CloudType.aws)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "VM image upgrade is only supported for AWS / GCP, got: " + provider.toString());
    }
    if (UniverseDefinitionTaskParams.hasEphemeralStorage(userIntent)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Cannot upgrade a universe with ephemeral storage.");
    }

    if ((machineImages == null || machineImages.isEmpty()) && imageBundleUUID == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "machineImages/imageBundle param is required.");
    }

    nodeToRegion.clear();
    for (NodeDetails node : universe.getUniverseDetails().nodeDetailsSet) {
      if (node.isMaster || node.isTserver) {
        Region region =
            AvailabilityZone.maybeGet(node.azUuid)
                .map(az -> az.getRegion())
                .orElseThrow(
                    () ->
                        new PlatformServiceException(
                            Status.BAD_REQUEST,
                            "Could not find region for AZ " + node.cloudInfo.az));

        if (machineImages != null && !machineImages.containsKey(region.getUuid())) {
          throw new PlatformServiceException(
              Status.BAD_REQUEST, "No VM image was specified for region " + node.cloudInfo.region);
        } else if (imageBundleUUID != null) {
          ImageBundle bundle = ImageBundle.getOrBadRequest(imageBundleUUID);
          if (bundle == null) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                String.format("Image bundle with UUID %s does not exist", imageBundleUUID));
          }
          if (bundle.getProvider().getCloudCode().equals(CloudType.aws)
              && !super.runtimeConfGetter.getStaticConf().getBoolean("yb.cloud.enabled")
              && !super.runtimeConfGetter.getGlobalConf(
                  GlobalConfKeys.disableImageBundleValidation)) {
            Map<String, ImageBundleDetails.BundleInfo> regionsBundleInfo =
                bundle.getDetails().getRegions();
            // Validate that the provided image bundle contains all the regions
            // that are present on the univserse being upgraded.
            CloudSpecificInfo cloudSpecificInfo = node.cloudInfo;
            if (!regionsBundleInfo.containsKey(cloudSpecificInfo.region)) {
              throw new PlatformServiceException(
                  Status.BAD_REQUEST,
                  String.format(
                      "Image Bundle %s is missing AMI ID for region %s",
                      bundle.getName(), cloudSpecificInfo.region));
            }
          }
        }

        nodeToRegion.putIfAbsent(node.nodeUuid, region.getUuid());
      }
    }
  }

  public static class Converter extends BaseConverter<VMImageUpgradeParams> {}
}
