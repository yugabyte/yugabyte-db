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

import com.yugabyte.yw.commissioner.*;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudAccessKeySetup;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudInitializer;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudRegionSetup;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudSetup;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import io.swagger.annotations.ApiModel;
import play.libs.Json;

import javax.inject.Inject;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

public class CloudBootstrap extends CloudTaskBase {
  @Inject
  protected CloudBootstrap(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @ApiModel("CloudBootstrapParams")
  public static class Params extends CloudTaskParams {
    public static Params fromProvider(Provider provider) {
      Params taskParams = new Params();
      taskParams.airGapInstall = provider.airGapInstall;
      taskParams.customHostCidrs = provider.customHostCidrs;
      taskParams.destVpcId = provider.destVpcId;
      taskParams.hostVpcId = provider.hostVpcId;
      taskParams.hostVpcRegion = provider.hostVpcRegion;
      taskParams.keyPairName = provider.keyPairName;
      taskParams.providerUUID = provider.uuid;
      taskParams.sshPort = provider.sshPort;
      taskParams.sshPrivateKeyContent = provider.sshPrivateKeyContent;
      taskParams.sshUser = provider.sshUser;
      taskParams.perRegionMetadata =
          provider
              .regions
              .stream()
              .collect(Collectors.toMap(region -> region.name, PerRegionMetadata::fromRegion));
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
      // TODO: Remove. This is not used currently.
      public String vpcCidr;

      // Custom map from AZ name to Subnet ID for AWS.
      // Default: created by YB.
      // Required: True for custom input, False for YW managed.
      public Map<String, String> azToSubnetIds;

      // Region Subnet ID for GCP.
      // Default: created by YB.
      // Required: True for custom input, False for YW managed.
      public String subnetId;

      // TODO(bogdan): does this not need a custom SSH user as well???
      // Custom AMI ID to use for YB nodes.
      // Default: hardcoded in devops.
      // Required: False.
      public String customImageId;

      // Custom SG ID to use for the YB nodes.
      // Default: created by YB.
      // Required: True for custom input, False for YW managed.
      public String customSecurityGroupId;

      public static PerRegionMetadata fromRegion(Region region) {
        PerRegionMetadata perRegionMetadata = new PerRegionMetadata();
        perRegionMetadata.customImageId = region.ybImage;
        perRegionMetadata.customSecurityGroupId = region.getSecurityGroupId();
        //    perRegionMetadata.subnetId = can only be set per zone
        perRegionMetadata.vpcId = region.getVnetName();
        //    perRegionMetadata.vpcCidr = never used
        perRegionMetadata.azToSubnetIds =
            region.zones.stream().collect(Collectors.toMap(zone -> zone.name, zone -> zone.subnet));

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
    public Integer sshPort = 54422;

    public String hostVpcId = null;
    public String hostVpcRegion = null;
    public List<String> customHostCidrs = new ArrayList<>();
    // TODO(bogdan): only used/needed for GCP.
    public String destVpcId = null;
  }

  // TODO: these fields should probably be persisted with provider but currently these are lost
  public static class ProviderTransientData {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
    Provider p = Provider.get(taskParams().providerUUID);
    if (p.code.equals(Common.CloudType.gcp.toString())
        || p.code.equals(Common.CloudType.aws.toString())
        || p.code.equals(Common.CloudType.azu.toString())) {
      createCloudSetupTask()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingCloud);
    }
    taskParams()
        .perRegionMetadata
        .forEach(
            (regionCode, metadata) -> {
              createRegionSetupTask(regionCode, metadata)
                  .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingRegion);
            });
    taskParams()
        .perRegionMetadata
        .forEach(
            (regionCode, metadata) -> {
              createAccessKeySetupTask(regionCode)
                  .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreateAccessKey);
            });

    createInitializerTask()
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.InitializeCloudMetadata);

    subTaskGroupQueue.run();
  }

  public SubTaskGroup createCloudSetupTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("Create Cloud setup task", executor);

    CloudSetup.Params params = new CloudSetup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.customPayload = Json.stringify(Json.toJson(taskParams()));
    CloudSetup task = createTask(CloudSetup.class);
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createRegionSetupTask(String regionCode, Params.PerRegionMetadata metadata) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("Create Region task", executor);

    CloudRegionSetup.Params params = new CloudRegionSetup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    params.metadata = metadata;
    params.destVpcId = taskParams().destVpcId;

    CloudRegionSetup task = createTask(CloudRegionSetup.class);
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createAccessKeySetupTask(String regionCode) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("Create Access Key", executor);
    CloudAccessKeySetup.Params params = new CloudAccessKeySetup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    params.keyPairName = taskParams().keyPairName;
    params.sshPrivateKeyContent = taskParams().sshPrivateKeyContent;
    params.sshUser = taskParams().sshUser;
    params.sshPort = taskParams().sshPort;
    params.airGapInstall = taskParams().airGapInstall;
    CloudAccessKeySetup task = createTask(CloudAccessKeySetup.class);
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createInitializerTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("Create Cloud initializer task", executor);
    CloudInitializer.Params params = new CloudInitializer.Params();
    params.providerUUID = taskParams().providerUUID;
    CloudInitializer task = createTask(CloudInitializer.class);
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }
}
