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
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;
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

  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.18.0.0")
  @ApiModelProperty(
      value =
          "Map of region UUID to AMI name. <b style=\"color:#ff0000\">Deprecated since "
              + "YBA version 2.18.0.0.</b> Use imageBundle instead.",
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
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.18.0.0")
  @ApiModelProperty(
      value =
          "Map of region UUID to SSH User override. <b style=\"color:#ff0000\">Deprecated since "
              + "YBA version 2.18.0.0.</b> Use imageBundle instead.",
      required = false,
      example = "{\n    'b28e0813-4866-4a2d-89f3-52265766d666': 'ec2-user',\n  }")
  public Map<UUID, String> sshUserOverrideMap = new HashMap<>();

  @ApiModelProperty(
      value =
          "ImageBundle to be used for upgrade. <b style=\"color:#ff0000\">Deprecated since "
              + "YBA version 2.21.1.0.</b> Use imageBundles instead.")
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.21.1.0")
  public UUID imageBundleUUID;

  @ApiModelProperty(
      value =
          "Available since YBA version 2.21.1.0. "
              + "ImageBundles for provider to be used for upgrade")
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.21.1.0")
  public List<ImageBundleUpgradeInfo> imageBundles = new ArrayList<>();

  public boolean forceVMImageUpgrade = false;
  public String ybSoftwareVersion = null;

  @JsonIgnore public final Map<UUID, UUID> nodeToRegion = new HashMap<>();
  @JsonIgnore public boolean isSoftwareUpdateViaVm = false;
  @JsonIgnore public VmUpgradeTaskType vmUpgradeTaskType = VmUpgradeTaskType.None;

  public VMImageUpgradeParams() {}

  @JsonCreator
  public VMImageUpgradeParams(
      @JsonProperty(value = "machineImages", required = false) Map<UUID, String> machineImages,
      @JsonProperty(value = "imageBundleUUID", required = false) UUID imageBundleUUID,
      @JsonProperty(value = "imageBundles", required = false)
          List<ImageBundleUpgradeInfo> imageBundles) {
    this.machineImages = machineImages;
    this.imageBundleUUID = imageBundleUUID;
    if (imageBundles != null) {
      this.imageBundles = imageBundles;
    }
  }

  @Override
  // This method is not just doing verification. It is also setting instance members
  // which are used in other methods.
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

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
    if (!(provider == CloudType.gcp || provider == CloudType.aws || provider == CloudType.azu)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "VM image upgrade is only supported for cloud providers, got: " + provider.toString());
    }
    if (UniverseDefinitionTaskParams.hasEphemeralStorage(universe.getUniverseDetails())) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Cannot upgrade a universe with ephemeral storage.");
    }

    if ((machineImages == null || machineImages.isEmpty())
        && imageBundleUUID == null
        && (imageBundles == null || imageBundles.size() == 0)) {
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
          // Populate the imageBundle in imageBundles
          ImageBundleUpgradeInfo bundleUpgradeInfo =
              new ImageBundleUpgradeInfo(node.placementUuid, imageBundleUUID);
          if (imageBundles.stream()
              .noneMatch(iB -> iB.getClusterUuid().equals(node.placementUuid))) {
            validateBundleInfo(universe, node, bundleUpgradeInfo);
            imageBundles.add(bundleUpgradeInfo);
          }
        } else if (imageBundles != null && imageBundles.size() > 0) {
          boolean imageSpecifiedForCluster =
              imageBundles.stream()
                  .map(bundle -> bundle.getClusterUuid())
                  .anyMatch(
                      placementUuid ->
                          placementUuid != null && placementUuid.equals(node.placementUuid));
          if (!imageSpecifiedForCluster) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                String.format("Specify the imageBundle for the cluster %s ", node.placementUuid));
          }

          imageBundles.forEach(
              bundleUpgradeInfo -> {
                if (bundleUpgradeInfo.getClusterUuid() == null) {
                  throw new PlatformServiceException(
                      Status.BAD_REQUEST,
                      String.format(
                          "Specify the placementInfo for which the bundle %s needs to be used.",
                          bundleUpgradeInfo.getImageBundleUuid()));
                }
                validateBundleInfo(universe, node, bundleUpgradeInfo);
              });
        }

        nodeToRegion.putIfAbsent(node.nodeUuid, region.getUuid());
      }
    }
  }

  public void validateBundleInfo(
      Universe universe, NodeDetails node, ImageBundleUpgradeInfo bundleUpgradeInfo) {
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getCluster(bundleUpgradeInfo.getClusterUuid());
    ImageBundle bundle =
        ImageBundle.getOrBadRequest(
            UUID.fromString(cluster.userIntent.provider), bundleUpgradeInfo.getImageBundleUuid());
    if (bundle == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          String.format("Image bundle with UUID %s does not exist", imageBundleUUID));
    }
    if (bundle.getProvider().getCloudCode().equals(CloudType.aws)
        && !super.runtimeConfGetter.getStaticConf().getBoolean("yb.cloud.enabled")
        && !super.runtimeConfGetter.getGlobalConf(GlobalConfKeys.disableImageBundleValidation)) {
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

  public static class Converter extends BaseConverter<VMImageUpgradeParams> {}

  // This class specifies the mapping to be used for performing the VM image upgrade.
  @AllArgsConstructor
  @Data
  @NoArgsConstructor
  public static class ImageBundleUpgradeInfo {
    private UUID clusterUuid;
    private UUID imageBundleUuid;
  }
}
