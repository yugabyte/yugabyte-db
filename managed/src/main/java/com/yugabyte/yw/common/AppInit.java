// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.Util.setYbaVersion;
import static com.yugabyte.yw.models.MetricConfig.METRICS_CONFIG_PATH;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.google.inject.name.Named;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.aws.AWSInitializer;
import com.yugabyte.yw.commissioner.BackupGarbageCollector;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.NodeAgentPoller;
import com.yugabyte.yw.commissioner.PerfAdvisorGarbageCollector;
import com.yugabyte.yw.commissioner.PerfAdvisorScheduler;
import com.yugabyte.yw.commissioner.PitrConfigPoller;
import com.yugabyte.yw.commissioner.RefreshKmsService;
import com.yugabyte.yw.commissioner.SetUniverseKey;
import com.yugabyte.yw.commissioner.SupportBundleCleanup;
import com.yugabyte.yw.commissioner.TaskGarbageCollector;
import com.yugabyte.yw.commissioner.YbcUpgrade;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudImageBundleSetup;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.AlertsGarbageCollector;
import com.yugabyte.yw.common.alerts.QueryAlerts;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.metrics.PlatformMetricsProcessor;
import com.yugabyte.yw.common.metrics.SwamperTargetsFileUpdater;
import com.yugabyte.yw.common.operator.KubernetesOperator;
import com.yugabyte.yw.common.services.FileDataService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ExtraMigration;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.MetricConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.queries.QueryHelper;
import com.yugabyte.yw.scheduler.Scheduler;
import io.ebean.DB;
import io.prometheus.client.hotspot.DefaultExports;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import play.Application;
import play.Environment;

/** We will use this singleton to do actions specific to the app environment, like db seed etc. */
@Slf4j
@Singleton
public class AppInit {

  private static final long MAX_APP_INITIALIZATION_TIME = 30;

  @Inject
  public AppInit(
      Environment environment,
      Application application,
      ConfigHelper configHelper,
      ReleaseManager releaseManager,
      GFlagsValidation gFlagsValidation,
      AWSInitializer awsInitializer,
      CustomerTaskManager taskManager,
      YamlWrapper yaml,
      ExtraMigrationManager extraMigrationManager,
      PitrConfigPoller pitrConfigPoller,
      TaskGarbageCollector taskGC,
      SetUniverseKey setUniverseKey,
      RefreshKmsService refreshKmsService,
      BackupGarbageCollector backupGC,
      PerfAdvisorScheduler perfAdvisorScheduler,
      PlatformReplicationManager replicationManager,
      AlertsGarbageCollector alertsGC,
      QueryAlerts queryAlerts,
      AlertConfigurationWriter alertConfigurationWriter,
      SwamperTargetsFileUpdater swamperTargetsFileUpdater,
      AlertConfigurationService alertConfigurationService,
      AlertDestinationService alertDestinationService,
      QueryHelper queryHelper,
      PlatformMetricsProcessor platformMetricsProcessor,
      Scheduler scheduler,
      CallHome callHome,
      HealthChecker healthChecker,
      ShellLogsManager shellLogsManager,
      Config config,
      SupportBundleCleanup supportBundleCleanup,
      NodeAgentPoller nodeAgentPoller,
      YbcUpgrade ybcUpgrade,
      PerfAdvisorGarbageCollector perfRecGC,
      SnapshotCleanup snapshotCleanup,
      FileDataService fileDataService,
      KubernetesOperator kubernetesOperator,
      RuntimeConfGetter confGetter,
      PrometheusConfigManager prometheusConfigManager,
      ImageBundleUtil imageBundleUtil,
      @Named("AppStartupTimeMs") Long startupTime)
      throws ReflectiveOperationException {
    try {
      log.info("Yugaware Application has started");

      if (!environment.isTest()) {
        // Check if we have provider data, if not, we need to seed the database
        if (Customer.find.query().where().findCount() == 0 && config.getBoolean("yb.seedData")) {
          log.debug("Seed the Yugaware DB");

          List<?> all =
              yaml.load(environment.resourceAsStream("db_seed.yml"), application.classloader());
          DB.saveAll(all);
          Customer customer = Customer.getAll().get(0);
          alertDestinationService.createDefaultDestination(customer.getUuid());
          alertConfigurationService.createDefaultConfigs(customer);
        }

        String storagePath = AppConfigHelper.getStoragePath();
        // Fix up DB paths if necessary
        if (config.getBoolean("yb.fixPaths")) {
          log.debug("Fixing up file paths.");
          fileDataService.fixUpPaths(storagePath);
          releaseManager.fixFilePaths();
        }

        boolean ywFileDataSynced =
            Boolean.parseBoolean(
                configHelper
                    .getConfig(ConfigHelper.ConfigType.FileDataSync)
                    .getOrDefault("synced", "false")
                    .toString());
        fileDataService.syncFileData(storagePath, ywFileDataSynced);

        String devopsHome = config.getString("yb.devops.home");
        if (devopsHome == null || devopsHome.length() == 0) {
          throw new RuntimeException("yb.devops.home is not set in application.conf");
        }

        if (storagePath == null || storagePath.length() == 0) {
          throw new RuntimeException(("yb.storage.path is not set in application.conf"));
        }

        boolean vmOsPatchingEnabled = confGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching);
        Map<String, Object> defaultYbaOsVersion =
            configHelper.getConfig(ConfigHelper.ConfigType.YBADefaultAMI);

        // temporarily revert due to PLAT-2434
        // LogUtil.updateApplicationLoggingFromConfig(sConfigFactory, config);
        // LogUtil.updateAuditLoggingFromConfig(sConfigFactory, config);

        // Initialize AWS if any of its instance types have an empty volumeDetailsList
        List<Provider> providerList = Provider.find.query().where().findList();
        for (Provider provider : providerList) {
          if (provider.getCode().equals("aws")) {
            for (InstanceType instanceType : InstanceType.findByProvider(provider, confGetter)) {
              if (instanceType.getInstanceTypeDetails() != null
                  && (instanceType.getInstanceTypeDetails().volumeDetailsList == null
                      || (instanceType.getInstanceTypeDetails().arch == null
                          && vmOsPatchingEnabled))) {
                // We started persisting all the instance types for a provider now, given that we
                // can manage multiple architecture via image bundle. This will ensure that we
                // have all the instance_types populated for the AWS providers.
                awsInitializer.initialize(provider.getCustomerUUID(), provider.getUuid());
                break;
              }
            }

            if (vmOsPatchingEnabled) {
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
          }
          if (vmOsPatchingEnabled && defaultYbaOsVersion != null) {
            String providerCode = provider.getCode();
            if (defaultYbaOsVersion.containsKey(providerCode)) {
              Map<String, String> currOSVersionDBMap =
                  (Map<String, String>) defaultYbaOsVersion.get(providerCode);
              if (currOSVersionDBMap != null
                  && currOSVersionDBMap.containsKey("version")
                  && !currOSVersionDBMap
                      .get("version")
                      .equals(CloudImageBundleSetup.CLOUD_OS_MAP.get(providerCode).getVersion())) {
                // In case defaultYbaAmiVersion is not null & not equal to version specified in
                // CloudImageBundleSetup.YBA_AMI_VERSION, we will check in the provider bundles
                // & migrate all the YBA_DEFAULT -> YBA_DEPRECATED, & at the same time generating
                // new bundle with the latest AMIs. This will only hold in case the provider
                // does not have CUSTOM bundles.
                imageBundleUtil.migrateImageBundlesForProviders(provider);
              }
            }
          }
        }

        if (vmOsPatchingEnabled) {
          // Store the latest YBA_AMI_VERSION in the yugaware_roperty.
          Map<String, Object> defaultYbaOsVersionMap =
              new HashMap<>(CloudImageBundleSetup.CLOUD_OS_MAP);
          configHelper.loadConfigToDB(ConfigType.YBADefaultAMI, defaultYbaOsVersionMap);
        }

        // Load metrics configurations.
        Map<String, Object> configs =
            yaml.load(environment.resourceAsStream(METRICS_CONFIG_PATH), application.classloader());
        MetricConfig.loadConfig(configs);

        // Enter all the configuration data. This is the first thing that should be
        // done as the other init steps may depend on this data.
        configHelper.loadConfigsToDB(environment);
        configHelper.loadSoftwareVersiontoDB(environment);

        // Run and delete any extra migrations.
        for (ExtraMigration m : ExtraMigration.getAll()) {
          m.run(extraMigrationManager);
        }

        // Import new local releases into release metadata
        releaseManager.importLocalReleases();
        releaseManager.updateCurrentReleases();
        releaseManager
            .getLocalReleases()
            .forEach(
                (version, rm) -> {
                  try {
                    gFlagsValidation.addDBMetadataFiles(version, rm);
                  } catch (Exception e) {
                    log.error("Error: ", e);
                  }
                });
        // Background thread to query for latest ARM release version.
        Thread armReleaseThread =
            new Thread(
                () -> {
                  try {
                    log.info("Attempting to query latest ARM release link.");
                    releaseManager.findLatestArmRelease(
                        ConfigHelper.getCurrentVersion(environment));
                    log.info("Imported ARM release download link.");
                  } catch (Exception e) {
                    log.warn("Error importing ARM release download link", e);
                  }
                });
        armReleaseThread.start();

        // initialize prometheus exports
        DefaultExports.initialize();

        // Handle incomplete tasks
        taskManager.handleAllPendingTasks();
        taskManager.updateUniverseSoftwareUpgradeStateSet();

        // Fail all incomplete support bundle creations.
        supportBundleCleanup.markAllRunningSupportBundlesFailed();

        // Schedule garbage collection of old completed tasks in database.
        taskGC.start();
        alertsGC.start();
        perfRecGC.start();

        setUniverseKey.start();
        // Refreshes all the KMS providers. Useful for renewing tokens, ttls, etc.
        refreshKmsService.start();

        // Schedule garbage collection of backups
        backupGC.start();

        // Cleanup orphan snapshots
        snapshotCleanup.start();

        perfAdvisorScheduler.start();

        // Cleanup old support bundles
        supportBundleCleanup.start();

        platformMetricsProcessor.start();
        alertConfigurationWriter.start();
        swamperTargetsFileUpdater.start();

        replicationManager.init();

        scheduler.init();
        callHome.start();
        queryAlerts.start();
        healthChecker.initialize();
        shellLogsManager.startLogsGC();
        nodeAgentPoller.init();
        pitrConfigPoller.start();

        ybcUpgrade.start();

        prometheusConfigManager.updateK8sScrapeConfigs();

        if (confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)) {
          if (!HighAvailabilityConfig.isFollower()) {
            kubernetesOperator.init(
                confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace));
          } else {
            log.info("Detected follower instance, not initializing kubernetes operator");
          }
        }

        // Add checksums for all certificates that don't have a checksum.
        CertificateHelper.createChecksums();

        long elapsed = (System.currentTimeMillis() - startupTime) / 1000;
        String elapsedStr = String.valueOf(elapsed);
        if (elapsed > MAX_APP_INITIALIZATION_TIME) {
          log.warn("Completed initialization in " + elapsedStr + " seconds.");
        } else {
          log.info("Completed initialization in " + elapsedStr + " seconds.");
        }

        setYbaVersion(ConfigHelper.getCurrentVersion(environment));

        // Fix up DB paths again to handle any new files (ybc) that moved during AppInit.
        if (config.getBoolean("yb.fixPaths")) {
          log.debug("Fixing up file paths a second time.");
          fileDataService.fixUpPaths(storagePath);
          releaseManager.fixFilePaths();
        }

        log.info("AppInit completed");
      }
    } catch (Throwable t) {
      log.error("caught error during app init ", t);
      throw t;
    }
  }
}
