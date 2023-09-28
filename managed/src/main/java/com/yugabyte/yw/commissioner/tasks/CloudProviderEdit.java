/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.base.Strings;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.CloudProviderHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.controllers.handlers.AccessKeyHandler;
import com.yugabyte.yw.controllers.handlers.RegionHandler;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.ImageBundleDetails.BundleInfo;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import io.swagger.annotations.ApiModel;
import java.time.Duration;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CloudProviderEdit extends CloudTaskBase {

  private RegionHandler regionHandler;
  private AccessKeyHandler accessKeyHandler;
  private CloudProviderHelper cloudProviderHelper;
  private ProviderEditRestrictionManager providerEditRestrictionManager;

  @Inject
  protected CloudProviderEdit(
      BaseTaskDependencies baseTaskDependencies,
      RegionHandler regionHandler,
      AccessKeyHandler accessKeyHandler,
      CloudProviderHelper cloudProviderHelper,
      ProviderEditRestrictionManager providerEditRestrictionManager) {
    super(baseTaskDependencies);
    this.regionHandler = regionHandler;
    this.accessKeyHandler = accessKeyHandler;
    this.cloudProviderHelper = cloudProviderHelper;
    this.providerEditRestrictionManager = providerEditRestrictionManager;
  }

  @ApiModel(value = "CloudProviderEditParams", description = "Parameters for editing provider")
  public static class Params extends CloudTaskParams {
    public Provider newProviderState;
    public boolean skipRegionBootstrap;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      Provider editProviderReq = taskParams().newProviderState;
      Provider provider = Provider.getOrBadRequest(taskParams().getProviderUUID());
      provider.setVersion(editProviderReq.getVersion());
      if (providerEditRestrictionManager.isAllowAutoTasksBeforeEdit()
          && !providerEditRestrictionManager.getTasksInUse(taskParams().providerUUID).isEmpty()) {
        waitForAutoTasks();
      }
      updateProviderData(provider, editProviderReq);
      updateAccessKeys(provider, editProviderReq);
      if (editProviderReq.getRegions() != null && !editProviderReq.getRegions().isEmpty()) {
        updateRegionsAndZones(provider, editProviderReq);
      }
      updateImageBundles(provider, editProviderReq);
      getRunnableTask().runSubTasks();
      provider = Provider.getOrBadRequest(taskParams().providerUUID);
      provider.setUsabilityState(Provider.UsabilityState.READY);
      provider.save();
      cloudProviderHelper.updatePrometheusConfig(provider);
    } catch (RuntimeException e) {
      log.error("Received exception during edit", e);
      Provider p = Provider.getOrBadRequest(taskParams().providerUUID);
      p.setUsabilityState(Provider.UsabilityState.ERROR);
      p.save();
      throw e;
    }
  }

  private void waitForAutoTasks() {
    long timeout = getMaxWaitMs();
    long waitTs = getWaitDurationMs();
    Duration waitDuration = Duration.ofMillis(waitTs);
    Collection<UUID> taskUUIDs = Collections.emptyList();
    while (timeout > 0) {
      taskUUIDs = providerEditRestrictionManager.getTasksInUse(taskParams().providerUUID);
      if (taskUUIDs.isEmpty()) {
        return;
      }
      waitFor(waitDuration);
      timeout -= waitTs;
    }
    throw new RuntimeException(
        "Reached timeout of " + getMaxWaitMs() + " ms while waiting for tasks: " + taskUUIDs);
  }

  private void updateRegionsAndZones(Provider provider, Provider editProviderReq) {
    Map<String, Region> existingRegions =
        provider.getRegions().stream().collect(Collectors.toMap(r -> r.getCode(), r -> r));
    Set<Region> regionsToAdd = new HashSet<>();
    for (Region region : editProviderReq.getRegions()) {
      Region oldRegion = existingRegions.get(region.getCode());
      if (oldRegion == null) {
        regionsToAdd.add(region);
      } else {
        if (oldRegion.isUpdateNeeded(region)) {
          log.debug("Editing region {}", region.getCode());
          if (provider.getCloudCode() == Common.CloudType.kubernetes) {
            cloudProviderHelper.bootstrapKubernetesProvider(
                provider, editProviderReq, Collections.singletonList(region), true);
          } else {
            regionHandler.editRegion(
                provider.getCustomerUUID(), provider.getUuid(), oldRegion.getUuid(), region);
          }
        } else if (!region.isActive() && oldRegion.isActive()) {
          log.debug("Deleting region {}", region.getCode());
          regionHandler.deleteRegion(
              provider.getCustomerUUID(), provider.getUuid(), region.getUuid());
          removeRegionReferenceFromImageBundles(editProviderReq, region.getCode());
        }
        cloudProviderHelper.updateAZs(provider, editProviderReq, region, oldRegion);
      }
    }
    if (regionsToAdd.size() > 0) {
      addRegions(provider, editProviderReq, regionsToAdd);
    }
  }

  public void addRegions(Provider provider, Provider editProviderReq, Set<Region> regionsToAdd) {
    log.debug("Adding regions {} to provider {}", regionsToAdd, provider.getName());
    if (provider.getCloudCode() == Common.CloudType.kubernetes) {
      cloudProviderHelper.editKubernetesProvider(provider, editProviderReq, regionsToAdd);
    } else {
      for (Region region : regionsToAdd) {
        CloudBootstrap.Params.PerRegionMetadata metadata =
            CloudBootstrap.Params.PerRegionMetadata.fromRegion(region);

        createRegionSetupTask(region.getCode(), metadata, getDestVpcId(provider))
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingRegion);

        CloudBootstrap.Params bootstrapParams =
            CloudBootstrap.Params.fromProvider(provider, editProviderReq);
        // In case the providerBootstrap fails, we won't be having accessKey setup
        // for the provider yet.
        if (provider.getAllAccessKeys() != null && provider.getAllAccessKeys().size() > 0) {
          bootstrapParams.keyPairName = AccessKey.getLatestKey(provider.getUuid()).getKeyCode();
          bootstrapParams.sshPrivateKeyContent = null;
          bootstrapParams.skipKeyValidateAndUpload = false;
        }
        createAccessKeySetupTask(bootstrapParams, region.getCode())
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreateAccessKey);
        // Need not to init CloudInitializer task for onprem provider.
        if (!provider.getCloudCode().equals(Common.CloudType.onprem)) {
          createInitializerTask()
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.InitializeCloudMetadata);
        }
      }
    }
  }

  private String getDestVpcId(Provider provider) {
    if (provider.getCloudCode() == Common.CloudType.gcp) {
      GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
      return gcpCloudInfo.getDestVpcId();
    }
    return null;
  }

  /*
   * Utility to remoe the region reference from the image Bundles
   * for AWS providers on region deletion.
   */
  private void removeRegionReferenceFromImageBundles(Provider provider, String regionCode) {
    if (provider.getCloudCode() != CloudType.aws) {
      // continue;
    }

    List<ImageBundle> bundles = provider.getImageBundles();
    if (bundles != null && bundles.size() > 0) {
      for (ImageBundle bundle : bundles) {
        if (bundle.getDetails() != null) {
          ImageBundleDetails details = bundle.getDetails();
          Map<String, BundleInfo> regionBundleInfo = details.getRegions();

          if (regionBundleInfo != null && regionBundleInfo.containsKey(regionCode)) {
            regionBundleInfo.remove(regionCode);
            details.setRegions(regionBundleInfo);
            bundle.setDetails(details);
          }
        }
      }
    }
  }

  private boolean updateProviderData(Provider provider, Provider editProviderReq) {
    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(editProviderReq);
    boolean updated = false;
    if (!provider.getName().equals(editProviderReq.getName())) {
      List<Provider> providers =
          Provider.getAll(
              provider.getCustomerUUID(), editProviderReq.getName(), provider.getCloudCode());
      if (providers.size() > 0) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format("Provider with name %s already exists.", editProviderReq.getName()));
      }
      provider.setName(editProviderReq.getName());
      updated = true;
    }
    if (!provider.getDetails().equals(editProviderReq.getDetails())) {
      updated = true;
      provider.setDetails(editProviderReq.getDetails());
    }
    // Compare the cloudInfo properties.
    if (!provider.getDetails().getCloudInfo().equals(editProviderReq.getDetails().getCloudInfo())) {
      provider.getDetails().setCloudInfo(editProviderReq.getDetails().getCloudInfo());
      if (provider.getCloudCode().equals(Common.CloudType.kubernetes)) {
        cloudProviderHelper.updateKubeConfig(provider, providerConfig, true);
      } else {
        cloudProviderHelper.maybeUpdateCloudProviderConfig(provider, providerConfig);
      }
      updated = true;
    }
    if (updated) {
      // Should not increment the version number in case of no change.
      provider.save();
    }
    return updated;
  }

  private boolean updateAccessKeys(Provider provider, Provider editProviderReq) {
    if (provider.getCloudCode().equals(Common.CloudType.kubernetes)) {
      // For k8s provider, access keys does not exist.
      return false;
    }
    /*
     * For the access key edits, user can
     * 1. Switch from YBA Managed <-> Self Managed, & vice-versa.
     * 2. Update the key Contents for the Self Managed Key.
     * In case no access key is specified we will create YBA managed access key.
     * In case sshPrivateKeyContent is specified we will create a Self Managed access key
     * with the content provider.
     * In case, keys are specified, that will be treated as no-op from access keys POV.
     */
    boolean result = false;
    List<AccessKey> accessKeys = editProviderReq.getAllAccessKeys();
    if (accessKeys.size() == 0) {
      // This is the case for adding YBA managed accessKey to the provider.
      result = true;
      accessKeyHandler.doEdit(provider, null, null);
    }

    for (AccessKey accessKey : accessKeys) {
      if (!Strings.isNullOrEmpty(accessKey.getKeyInfo().sshPrivateKeyContent)
          && accessKey.getIdKey() == null) {
        /*
         * If the user has provided the accessKey content, this will be the case of
         * Self Managed Keys, create a new Key, & append with other keys.
         */
        result = true;
        accessKeyHandler.doEdit(provider, accessKey, null);
      }
    }

    return result;
  }

  private void updateImageBundles(Provider provider, Provider editProviderReq) {
    if (!provider.getCloudCode().imageBundleSupported()) {
      return;
    }
    createUpdateImageBundleTask(provider, editProviderReq.getImageBundles());
  }

  private long getWaitDurationMs() {
    return confGetter.getGlobalConf(GlobalConfKeys.waitForProviderTasksStepMs);
  }

  private long getMaxWaitMs() {
    return confGetter.getGlobalConf(GlobalConfKeys.waitForProviderTasksTimeoutMs);
  }
}
