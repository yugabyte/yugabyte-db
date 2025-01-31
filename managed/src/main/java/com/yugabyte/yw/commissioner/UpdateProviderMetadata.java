// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.commissioner;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.aws.AWSInitializer;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudImageBundleSetup;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.Provider;
import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class UpdateProviderMetadata {
  // Async helper class to update the provider metadata.
  // We update the architecture details in the instance object in case
  // that is not present.
  // Also, generates the YBA managed ImageBundles in case YBA upgrades the
  // bundles.

  private final RuntimeConfGetter confGetter;
  private final ConfigHelper configHelper;
  private final AWSInitializer awsInitializer;
  private final ImageBundleUtil imageBundleUtil;
  private final ProviderEditRestrictionManager providerEditRestrictionManager;
  private final PlatformScheduler platformScheduler;

  @Inject
  public UpdateProviderMetadata(
      RuntimeConfGetter confGetter,
      ConfigHelper configHelper,
      AWSInitializer awsInitializer,
      ImageBundleUtil imageBundleUtil,
      ProviderEditRestrictionManager providerEditRestrictionManager,
      PlatformScheduler platformScheduler) {
    this.confGetter = confGetter;
    this.configHelper = configHelper;
    this.awsInitializer = awsInitializer;
    this.imageBundleUtil = imageBundleUtil;
    this.providerEditRestrictionManager = providerEditRestrictionManager;
    this.platformScheduler = platformScheduler;
  }

  public void start() {
    boolean vmOsPatchingEnabled = confGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching);
    if (vmOsPatchingEnabled) {
      log.info("Started Provider metadata udpate task");
      platformScheduler.scheduleOnce(
          getClass().getSimpleName(),
          Duration.ZERO, // InitialDelay
          this::scheduleRunner);
    }
  }

  public void scheduleRunner() {

    Map<String, Object> defaultYbaOsVersion =
        configHelper.getConfig(ConfigHelper.ConfigType.YBADefaultAMI);

    List<Provider> providerList = Provider.find.query().where().findList();
    for (Provider provider : providerList) {
      if (provider.getUsabilityState() != Provider.UsabilityState.READY) {
        log.info(
            String.format(
                "Provider %s is not in ready state. Skipping metadata update", provider.getName()));
        continue;
      }
      providerEditRestrictionManager.tryEditProvider(
          provider.getUuid(), () -> updateProviderMetadata(provider, defaultYbaOsVersion));
    }

    // Store the latest YBA_AMI_VERSION in the yugaware_roperty.
    Map<String, Object> defaultYbaOsVersionMap = new HashMap<>(CloudImageBundleSetup.CLOUD_OS_MAP);
    configHelper.loadConfigToDB(ConfigType.YBADefaultAMI, defaultYbaOsVersionMap);
  }

  public void updateProviderMetadata(Provider provider, Map<String, Object> defaultYbaOsVersion) {
    boolean updateFailed = false;
    provider.setUsabilityState(Provider.UsabilityState.UPDATING);
    provider.save();
    try {
      if (provider.getCode().equals("aws")) {
        for (InstanceType instanceType : InstanceType.findByProvider(provider, confGetter)) {
          if (instanceType.getInstanceTypeDetails() != null
              && (instanceType.getInstanceTypeDetails().volumeDetailsList == null
                  || (instanceType.getInstanceTypeDetails().arch == null))) {
            // We started persisting all the instance types for a provider now, given that
            // we
            // can manage multiple architecture via image bundle. This will ensure that we
            // have all the instance_types populated for the AWS providers.
            awsInitializer.initialize(provider.getCustomerUUID(), provider.getUuid());
            break;
          }
        }

        // If there still exists instance types with arch as null, those will be
        // the custom added instance. Need to populate arch for those as well.
        List<InstanceType> instancesWithoutArch =
            InstanceType.getInstanceTypesWithoutArch(provider.getUuid());
        List<ImageBundle> defaultImageBundles =
            ImageBundle.getDefaultForProvider(provider.getUuid());
        if (instancesWithoutArch.size() > 0 && defaultImageBundles.size() > 0) {
          for (InstanceType instance : instancesWithoutArch) {
            if (instance.getInstanceTypeDetails() == null) {
              instance.setInstanceTypeDetails(new InstanceTypeDetails());
            }

            if (defaultImageBundles.get(0).getDetails() != null) {
              instance.getInstanceTypeDetails().arch =
                  defaultImageBundles.get(0).getDetails().getArch();
            }

            instance.save();
          }
        }
      }

      String providerCode = provider.getCode();
      Map<String, String> currOSVersionDBMap = null;
      if (defaultYbaOsVersion != null && defaultYbaOsVersion.containsKey(providerCode)) {
        currOSVersionDBMap = (Map<String, String>) defaultYbaOsVersion.get(providerCode);
      }
      if (imageBundleUtil.migrateYBADefaultBundles(currOSVersionDBMap, provider)) {
        // In case defaultYbaAmiVersion is not null & not equal to version specified in
        // CloudImageBundleSetup.YBA_AMI_VERSION, we will check in the provider bundles
        // & migrate all the YBA_DEFAULT -> YBA_DEPRECATED, & at the same time generating
        // new bundle with the latest AMIs. This will only hold in case the provider
        // does not have CUSTOM bundles.
        imageBundleUtil.migrateImageBundlesForProviders(provider);
      }
    } catch (Exception e) {
      log.error(
          String.format(
              "Metadata update failed for provider %s with %s",
              provider.getName(), e.getMessage()));
      provider.setUsabilityState(Provider.UsabilityState.ERROR);
      updateFailed = true;
    } finally {
      if (!updateFailed) {
        provider.setUsabilityState(Provider.UsabilityState.READY);
      }
      provider.save();
    }
  }
}
