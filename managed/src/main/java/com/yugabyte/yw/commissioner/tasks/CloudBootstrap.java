/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudImageBundleSetup;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudSetup;
import com.yugabyte.yw.common.CloudProviderHelper;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import io.swagger.annotations.ApiModel;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Retryable
public class CloudBootstrap extends CloudTaskBase {

  private CloudProviderHandler cloudProviderHandler;
  private CloudProviderHelper cloudProviderHelper;

  @Inject
  protected CloudBootstrap(
      BaseTaskDependencies baseTaskDependencies,
      CloudProviderHandler cloudProviderHandler,
      CloudProviderHelper cloudProviderHelper) {
    super(baseTaskDependencies);
    this.cloudProviderHandler = cloudProviderHandler;
    this.cloudProviderHelper = cloudProviderHelper;
  }

  @ApiModel(value = "CloudBootstrapParams", description = "Cloud bootstrap parameters")
  public static class Params extends CloudTaskParams {
    public static Params fromProvider(Provider provider) {
      return CloudBootstrap.Params.fromProvider(provider, provider);
    }

    public static Params fromProvider(Provider provider, Provider reqProvider) {
      Params taskParams = new Params();
      if (provider.getCloudCode() == CloudType.kubernetes) {
        return taskParams;
      }

      List<Region> regions = reqProvider.getRegions();
      // This is the case of initial provider creation.
      // If user provides his own access keys, we should take the first one in the list.
      // AccessKey in the provider object will be empty at this point as they are not yet
      // synced in the DB.
      if (reqProvider.getAllAccessKeys() != null && reqProvider.getAllAccessKeys().size() > 0) {
        AccessKey accessKey = reqProvider.getAllAccessKeys().get(0);
        taskParams.keyPairName = accessKey.getKeyInfo().keyPairName;
        taskParams.sshPrivateKeyContent = accessKey.getKeyInfo().sshPrivateKeyContent;
        taskParams.skipKeyValidateAndUpload = accessKey.getKeyInfo().skipKeyValidateAndUpload;
      }
      String destVpcId = null;
      String hostVpcId = null;
      String hostVpcRegion = null;
      CloudType cloudType = provider.getCloudCode();
      if (cloudType.equals(CloudType.aws)) {
        AWSCloudInfo awsCloudInfo = CloudInfoInterface.get(provider);
        hostVpcId = awsCloudInfo.getHostVpcId();
        hostVpcRegion = awsCloudInfo.getHostVpcRegion();
      } else if (cloudType.equals(CloudType.gcp)) {
        GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
        hostVpcId = gcpCloudInfo.getHostVpcId();
        destVpcId = gcpCloudInfo.getDestVpcId();
      }
      taskParams.airGapInstall = provider.getDetails().airGapInstall;
      taskParams.destVpcId = destVpcId;
      taskParams.hostVpcId = hostVpcId;
      if (provider.getCloudCode().equals(CloudType.gcp)) {
        GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
        // useHostVpc will be false for the case when user wants yugabyte to
        // create & manage VPC on their behalf.
        if (gcpCloudInfo.getUseHostVPC() != null && !gcpCloudInfo.getUseHostVPC()) {
          taskParams.createNewVpc = true;
        }
      }
      taskParams.hostVpcRegion = hostVpcRegion;
      taskParams.providerUUID = provider.getUuid();
      taskParams.sshPort = provider.getDetails().sshPort;
      taskParams.sshUser = provider.getDetails().sshUser;
      taskParams.setUpChrony = provider.getDetails().setUpChrony;
      taskParams.ntpServers = provider.getDetails().ntpServers;
      taskParams.showSetUpChrony = provider.getDetails().showSetUpChrony;
      taskParams.skipProvisioning = provider.getDetails().skipProvisioning;
      taskParams.perRegionMetadata =
          regions.stream()
              .collect(Collectors.toMap(region -> region.getCode(), PerRegionMetadata::fromRegion));
      taskParams.imageBundles = reqProvider.getImageBundles();
      return taskParams;
    }

    // Class to encapsulate custom network bootstrap overrides per region.
    public static class PerRegionMetadata {
      // Custom VPC ID to use for this region
      // Default: created by YB.
      // Required: True for custom input, False for YW managed.
      public String vpcId;

      // Custom CIDR to use for the VPC, if YB is creating it.
      // Default: chosen by YB.
      // Required: False.
      public String vpcCidr;

      // Custom map from AZ name to Subnet ID for AWS.
      // Default: created by YB.
      // Required: True for custom input, False for YW managed.
      public Map<String, String> azToSubnetIds;

      // Custom map from AZ name to Subnet ID for AWS.
      // Default: Empty
      // Required: False for custom input, False for YW managed.
      public Map<String, String> azToSecondarySubnetIds = null;

      // Region Subnet ID for GCP.
      // Default: created by YB.
      // Required: True for custom input, False for YW managed.
      public String subnetId;

      // Region Secondary Subnet ID for GCP.
      // Default: Null
      // Required: False for custom input, False for YW managed.
      public String secondarySubnetId = null;

      // TODO(bogdan): does this not need a custom SSH user as well???
      // Custom AMI ID to use for YB nodes.
      // Default: hardcoded in devops.
      // Required: False.
      public String customImageId;

      // Custom SG ID to use for the YB nodes.
      // Default: created by YB.
      // Required: True for custom input, False for YW managed.
      public String customSecurityGroupId;

      // Required for configuring region for onprem provider.
      public String regionName;
      public double latitude;
      public double longitude;
      // List of zones for regions, to be used for only onprem usecase.
      public List<AvailabilityZone> azList;

      // Instance template to use for new YB nodes.
      // Default: Null.
      // Required: False.
      public String instanceTemplate = null;

      // Image architecture for region.
      // Default: x86_64
      public Architecture architecture;

      public static PerRegionMetadata fromRegion(Region region) {
        PerRegionMetadata perRegionMetadata = new PerRegionMetadata();
        perRegionMetadata.customImageId = region.getYbImage();
        perRegionMetadata.customSecurityGroupId = region.getSecurityGroupId();
        //    perRegionMetadata.subnetId = can only be set per zone
        perRegionMetadata.vpcId = region.getVnetName();
        perRegionMetadata.architecture =
            region.getArchitecture() != null ? region.getArchitecture() : Architecture.x86_64;
        // Instance templates are currently only implemented for GCP.
        if (region.getProviderCloudCode().equals(Common.CloudType.gcp)) {
          GCPRegionCloudInfo g = CloudInfoInterface.get(region);
          perRegionMetadata.instanceTemplate = g.instanceTemplate;
        }
        //    perRegionMetadata.vpcCidr = never used
        if (region.getZones() == null || region.getZones().size() == 0) {
          perRegionMetadata.azToSubnetIds = new HashMap<>();
        } else {
          perRegionMetadata.azToSubnetIds =
              region.getZones().stream()
                  .filter(zone -> zone.getName() != null && zone.getSubnet() != null)
                  .collect(Collectors.toMap(zone -> zone.getName(), zone -> zone.getSubnet()));
          // Check if the zones have a secondary subnet
          perRegionMetadata.azToSecondarySubnetIds =
              region.getZones().stream()
                  .filter(zone -> zone.getName() != null && zone.getSecondarySubnet() != null)
                  .collect(
                      Collectors.toMap(zone -> zone.getName(), zone -> zone.getSecondarySubnet()));
          // In case of GCP, we want to use the secondary subnet, which will be the same across
          // zones. Will be ignored in all other cases.
          perRegionMetadata.secondarySubnetId = region.getZones().get(0).getSecondarySubnet();
          perRegionMetadata.subnetId = region.getZones().get(0).getSubnet();

          if (region.getProviderCloudCode() == Common.CloudType.onprem) {
            // OnPrem provider specific fields.
            perRegionMetadata.latitude = region.getLatitude();
            perRegionMetadata.longitude = region.getLongitude();
            perRegionMetadata.azList = region.getZones();
            perRegionMetadata.regionName = region.getName();
          }
        }
        return perRegionMetadata;
      }
    }

    // Map from region name to metadata.
    public Map<String, PerRegionMetadata> perRegionMetadata = new HashMap<>();

    // Custom keypair name to use when spinning up YB nodes.
    // Default: created and managed by YB.
    public String keyPairName = null;

    // Custom SSH private key component.
    // Default: created and managed by YB.
    public String sshPrivateKeyContent = null;

    // Custom SSH user to login to machines.
    // Default: created and managed by YB.
    public String sshUser = null;

    // Whether provider should use airgapped install.
    // Default: false.
    public boolean airGapInstall = false;

    // Port to open for connections on the instance.
    public Integer sshPort = 22;

    public String hostVpcId = null;
    public String hostVpcRegion = null;
    public String destVpcId = null;
    public boolean createNewVpc = false;

    // Dictates whether or not NTP should be configured on newly provisioned nodes.
    public boolean setUpChrony = false;

    // Dictates which NTP servers should be configured on newly provisioned nodes.
    public List<String> ntpServers = new ArrayList<>();

    // Indicates whether the provider was created before or after PLAT-3009.
    // True if it was created after, else it was created before.
    // Dictates whether or not to show the set up NTP option in the provider UI.
    public boolean showSetUpChrony = true;

    public List<ImageBundle> imageBundles;

    // used for onprem nodes for the cases when manual provision is set.
    public boolean skipProvisioning = false;

    // used for skipping the key validation & upload for AWS provider.
    // See, AccessKey.KeyInfo for detailed summary on usage.
    public boolean skipKeyValidateAndUpload = false;

    // K8s provider specific params.
    public Provider reqProviderEbean;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Provider p = Provider.getOrBadRequest(taskParams().providerUUID);
    p.setUsabilityState(Provider.UsabilityState.UPDATING);
    p.save();
    Common.CloudType cloudType = Common.CloudType.valueOf(p.getCode());
    try {
      if (cloudType != CloudType.kubernetes) {
        if (cloudType.isRequiresBootstrap() && cloudType != CloudType.onprem) {
          createCloudSetupTask()
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingCloud);
        }
        taskParams()
            .perRegionMetadata
            .forEach(
                (regionCode, metadata) -> {
                  createRegionSetupTask(regionCode, metadata, taskParams().destVpcId, isFirstTry())
                      .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingRegion);
                });
        taskParams()
            .perRegionMetadata
            .forEach(
                (regionCode, metadata) -> {
                  createAccessKeySetupTask(taskParams(), regionCode, isFirstTry())
                      .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreateAccessKey);
                });

        // Need not to init CloudInitializer task for onprem provider.
        if (!p.getCloudCode().equals(CloudType.onprem)) {
          createCloudImageBundleSetupTask();
          createInitializerTask()
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.InitializeCloudMetadata);
        }
      } else {
        cloudProviderHandler.createKubernetesNew(
            taskParams().getProviderUUID(), taskParams().reqProviderEbean);
      }

      getRunnableTask().runSubTasks();
      p = Provider.getOrBadRequest(taskParams().providerUUID);
      p.setUsabilityState(Provider.UsabilityState.READY);
      p.save();

      cloudProviderHelper.updatePrometheusConfig(p);
    } catch (RuntimeException e) {
      log.error("Received exception during bootstrap", e);
      p = Provider.getOrBadRequest(taskParams().providerUUID);
      p.setUsabilityState(Provider.UsabilityState.ERROR);
      p.save();
      throw e;
    }
  }

  public SubTaskGroup createCloudSetupTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("Create Cloud setup task");
    CloudBootstrap.Params params =
        Json.fromJson(Json.toJson(taskParams()), CloudBootstrap.Params.class);
    CloudSetup task = createTask(CloudSetup.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createCloudImageBundleSetupTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("Create Image bundle setup task");
    CloudImageBundleSetup.Params params = new CloudImageBundleSetup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.imageBundles = taskParams().imageBundles;
    params.isFirstTry = isFirstTry();
    CloudImageBundleSetup task = createTask(CloudImageBundleSetup.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
