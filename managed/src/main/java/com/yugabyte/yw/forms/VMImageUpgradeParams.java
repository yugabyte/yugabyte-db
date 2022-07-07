// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
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

  public Map<UUID, String> machineImages = new HashMap<>();
  public boolean forceVMImageUpgrade = false;
  public String ybSoftwareVersion = null;

  @JsonIgnore public final Map<UUID, UUID> nodeToRegion = new HashMap<>();
  @JsonIgnore public boolean isSoftwareUpdateViaVm = false;
  @JsonIgnore public VmUpgradeTaskType vmUpgradeTaskType = VmUpgradeTaskType.None;

  public VMImageUpgradeParams() {}

  @JsonCreator
  public VMImageUpgradeParams(
      @JsonProperty(value = "machineImages", required = true) Map<UUID, String> machineImages) {
    this.machineImages = machineImages;
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

    if (machineImages.isEmpty()) {
      throw new PlatformServiceException(Status.BAD_REQUEST, "machineImages param is required.");
    }

    nodeToRegion.clear();
    for (NodeDetails node : universe.getUniverseDetails().nodeDetailsSet) {
      if (node.isMaster || node.isTserver) {
        Region region =
            AvailabilityZone.maybeGet(node.azUuid)
                .map(az -> az.region)
                .orElseThrow(
                    () ->
                        new PlatformServiceException(
                            Status.BAD_REQUEST,
                            "Could not find region for AZ " + node.cloudInfo.az));

        if (!machineImages.containsKey(region.uuid)) {
          throw new PlatformServiceException(
              Status.BAD_REQUEST, "No VM image was specified for region " + node.cloudInfo.region);
        }

        nodeToRegion.putIfAbsent(node.nodeUuid, region.uuid);
      }
    }
  }

  public static class Converter extends BaseConverter<VMImageUpgradeParams> {}
}
