// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.Util.setYbaVersion;
import static com.yugabyte.yw.models.MetricConfig.METRICS_CONFIG_PATH;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.google.inject.name.Named;
import com.jayway.jsonpath.JsonPath;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.aws.AWSInitializer;
import com.yugabyte.yw.commissioner.AutoMasterFailoverScheduler;
import com.yugabyte.yw.commissioner.BackupGarbageCollector;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.NodeAgentEnabler;
import com.yugabyte.yw.commissioner.NodeAgentPoller;
import com.yugabyte.yw.commissioner.PerfAdvisorGarbageCollector;
import com.yugabyte.yw.commissioner.PerfAdvisorScheduler;
import com.yugabyte.yw.commissioner.PitrConfigPoller;
import com.yugabyte.yw.commissioner.RefreshKmsService;
import com.yugabyte.yw.commissioner.SetUniverseKey;
import com.yugabyte.yw.commissioner.SlowQueriesAggregator;
import com.yugabyte.yw.commissioner.SupportBundleCleanup;
import com.yugabyte.yw.commissioner.TaskGarbageCollector;
import com.yugabyte.yw.commissioner.UpdateProviderMetadata;
import com.yugabyte.yw.commissioner.XClusterScheduler;
import com.yugabyte.yw.commissioner.YbcUpgrade;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.AlertsGarbageCollector;
import com.yugabyte.yw.common.alerts.QueryAlerts;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.diagnostics.ThreadDumpPublisher;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.metrics.PlatformMetricsProcessor;
import com.yugabyte.yw.common.metrics.SwamperTargetsFileUpdater;
import com.yugabyte.yw.common.operator.KubernetesOperator;
import com.yugabyte.yw.common.pa.EmbeddedCollectorInitializer;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.common.services.FileDataService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ExtraMigration;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.MetricConfig;
import com.yugabyte.yw.models.Principal;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import com.yugabyte.yw.scheduler.JobScheduler;
import com.yugabyte.yw.scheduler.Scheduler;
import db.migration.default_.common.R__Sync_System_Roles;
import io.ebean.DB;
import io.ebean.PagedList;
import io.prometheus.metrics.core.metrics.Gauge;
import io.prometheus.metrics.model.registry.PrometheusRegistry;
import java.security.Provider;
import java.security.Security;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.bouncycastle.crypto.CryptoServicesRegistrar;
import org.bouncycastle.crypto.fips.FipsStatus;
import play.Application;
import play.Environment;

/** We will use this singleton to do actions specific to the app environment, like db seed etc. */
@Slf4j
@Singleton
public class AppInit {

  private static final long MAX_APP_INITIALIZATION_TIME = 30;

  public static final Gauge INIT_TIME =
      Gauge.builder()
          .name("yba_init_time_seconds")
          .help("Last YBA startup time in seconds.")
          .register(PrometheusRegistry.defaultRegistry);

  private static final AtomicBoolean IS_H2_DB = new AtomicBoolean(false);

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
      AutoMasterFailoverScheduler autoMasterFailoverScheduler,
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
      ThreadDumpPublisher threadDumpPublisher,
      PlatformMetricsProcessor platformMetricsProcessor,
      Scheduler scheduler,
      CallHome callHome,
      HealthChecker healthChecker,
      ShellLogsManager shellLogsManager,
      Config config,
      SupportBundleCleanup supportBundleCleanup,
      NodeAgentPoller nodeAgentPoller,
      YbcUpgrade ybcUpgrade,
      XClusterScheduler xClusterScheduler,
      PerfAdvisorGarbageCollector perfRecGC,
      SnapshotCleanup snapshotCleanup,
      FileDataService fileDataService,
      KubernetesOperator kubernetesOperator,
      RuntimeConfGetter confGetter,
      PrometheusConfigManager prometheusConfigManager,
      UpdateProviderMetadata updateProviderMetadata,
      @Named("AppStartupTimeMs") Long startupTime,
      ReleasesUtils releasesUtils,
      JobScheduler jobScheduler,
      NodeAgentEnabler nodeAgentEnabler,
      RoleBindingUtil roleBindingUtil,
      SlowQueriesAggregator slowQueriesAggregator,
      EmbeddedCollectorInitializer embeddedCollectorInitializer)
      throws ReflectiveOperationException {
    try {
      log.info("Yugaware Application has started");
      setYbaVersion(ConfigHelper.getCurrentVersion(environment));
      String displayVersion = Util.getYbaVersion();
      if (config.getBoolean(CommonUtils.FIPS_ENABLED)) {
        displayVersion = displayVersion + " (FIPS)";
      }
      log.info("YBA version: {}", displayVersion);
      if (environment.isTest()) {
        String dbDriverKey = "db.default.driver";
        if (config.hasPath(dbDriverKey)) {
          String driver = config.getString(dbDriverKey);
          IS_H2_DB.set(driver.contains("org.h2.Driver"));
        }
      } else {
        // only start thread dump collection for YBM at this time
        if (config.getBoolean("yb.cloud.enabled")) {
          threadDumpPublisher.start();
        }

        // RBAC is on by default so sync roles for all customers.
        R__Sync_System_Roles.syncSystemRoles();

        // Check if we have provider data, if not, we need to seed the database
        if (Customer.find.query().where().findCount() == 0 && config.getBoolean("yb.seedData")) {
          log.debug("Seed the Yugaware DB");

          List<?> all =
              yaml.load(environment.resourceAsStream("db_seed.yml"), application.classloader());
          DB.saveAll(all);
          Customer customer = Customer.getAll().get(0);
          alertDestinationService.createDefaultDestination(customer.getUuid());
          alertConfigurationService.createDefaultConfigs(customer);
          // Create system roles for the newly created customer.
          R__Sync_System_Roles.syncSystemRoles(customer.getUuid());
          // Principal entry and role bindings for newly created users.
          for (Users user : Users.find.all()) {
            Principal principal = Principal.get(user.getUuid());
            if (principal == null) {
              log.info("Adding Principal entry for user with email: " + user.getEmail());
              new Principal(user).save();
            }
            ResourceGroup resourceGroup =
                ResourceGroup.getSystemDefaultResourceGroup(customer.getUuid(), user);
            // Create a single role binding for the user.
            Users.Role usersRole = user.getRole();
            Role newRbacRole = Role.get(customer.getUuid(), usersRole.name());
            RoleBinding createdRoleBinding =
                roleBindingUtil.createRoleBinding(
                    user.getUuid(),
                    newRbacRole.getRoleUUID(),
                    RoleBindingType.System,
                    resourceGroup);
            log.info(
                "Created system role binding for user '{}' (email '{}') of customer '{}', "
                    + "with role '{}' (name '{}'), and default role binding '{}'.",
                user.getUuid(),
                user.getEmail(),
                customer.getUuid(),
                newRbacRole.getRoleUUID(),
                newRbacRole.getName(),
                createdRoleBinding.toString());
          }
        }

        String storagePath = AppConfigHelper.getStoragePath();
        // Fix up DB paths if necessary
        if (config.getBoolean("yb.fixPaths")) {
          log.debug("Fixing up file paths.");
          fileDataService.fixUpPaths(storagePath);
          releaseManager.fixFilePaths();
        }
        // yb.fixPaths has a specific, limited use case. This should run always.
        releasesUtils.releaseUploadPathFixup();

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
            .getLocalReleaseVersions(false /* includeKubernetes */)
            .forEach(
                version -> {
                  try {
                    gFlagsValidation.addDBMetadataFiles(version);
                  } catch (Exception e) {
                    log.error("Error: ", e);
                  }
                });
        // Background thread to query for latest ARM release version.
        // Only run for non-cloud deployments, as YBM will add any necessary releases on their own.
        if (!config.getBoolean("yb.cloud.enabled")) {
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
        } else {
          log.debug("skipping fetch latest arm build for cloud enabled deployment");
        }

        Thread flagsThread =
            new Thread(
                () -> {
                  updateSensitiveGflagsforRedaction(gFlagsValidation);
                });
        flagsThread.start();

        // Handle incomplete tasks
        taskManager.handleRestoreTask();
        taskManager.handleAllPendingTasks();
        taskManager.updateUniverseSoftwareUpgradeStateSet();
        taskManager.handlePendingConsistencyTasks();
        taskManager.handleAutoRetryAbortedTasks();

        // Fail all incomplete support bundle creations.
        supportBundleCleanup.markAllRunningSupportBundlesFailed();

        // Cleanup any untracked uploaded releases
        releasesUtils.cleanupUntracked();

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

        slowQueriesAggregator.start();
        platformMetricsProcessor.start();
        alertConfigurationWriter.start();
        swamperTargetsFileUpdater.start();

        replicationManager.init();

        scheduler.init();
        jobScheduler.init();
        callHome.start();
        queryAlerts.start();
        healthChecker.initialize();
        shellLogsManager.startLogsGC();
        nodeAgentPoller.init();
        pitrConfigPoller.start();
        autoMasterFailoverScheduler.init();
        if (!HighAvailabilityConfig.isFollower()) {
          updateProviderMetadata.resetUpdatingProviders();
          // Update the provider metadata in case YBA updates the managed AMIs.
          updateProviderMetadata.start();
        }
        xClusterScheduler.start();

        ybcUpgrade.start();
        nodeAgentEnabler.init();

        prometheusConfigManager.updateK8sScrapeConfigs();

        if (confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)) {
          if (!HighAvailabilityConfig.isFollower()) {
            kubernetesOperator.init();
          } else {
            log.info("Detected follower instance, not initializing kubernetes operator");
          }
        }

        // Add checksums for all certificates that don't have a checksum.
        CertificateHelper.createChecksums();

        embeddedCollectorInitializer.initialize();

        long elapsed = (System.currentTimeMillis() - startupTime) / 1000;
        String elapsedStr = String.valueOf(elapsed);
        INIT_TIME.set(elapsed);
        if (elapsed > MAX_APP_INITIALIZATION_TIME) {
          log.warn("Completed initialization in " + elapsedStr + " seconds.");
        } else {
          log.info("Completed initialization in " + elapsedStr + " seconds.");
        }

        // Fix up DB paths again to handle any new files (ybc) that moved during AppInit.
        if (config.getBoolean("yb.fixPaths")) {
          log.debug("Fixing up file paths a second time.");
          fileDataService.fixUpPaths(storagePath);
          releaseManager.fixFilePaths();
        }

        if (config.getBoolean(CommonUtils.FIPS_ENABLED)) {
          if (FipsStatus.isReady()) {
            log.info("FipsStatus.isReady = true");
          } else {
            throw new RuntimeException("FipsStatus.isReady = false");
          }
          if (CryptoServicesRegistrar.isInApprovedOnlyMode()) {
            log.info("CryptoServicesRegistrar.isInApprovedOnlyMode = true");
          } else {
            throw new RuntimeException("CryptoServicesRegistrar.isInApprovedOnlyMode = false");
          }
          Provider[] providers = Security.getProviders();
          log.info("Following providers are installed:");
          if (providers != null) {
            for (int i = 0; i < providers.length; i++) {
              log.info("{}: {}", i, providers[i].getName());
            }
          }
        }

        log.info("AppInit completed");
      }
    } catch (Throwable t) {
      log.error("caught error during app init ", t);
      throw t;
    }
  }

  // Workaround for some tests with H2 database.
  public static boolean isH2Db() {
    return IS_H2_DB.get();
  }

  private void updateSensitiveGflagsforRedaction(GFlagsValidation gFlagsValidation) {
    int pageSize = 50, pageIndex = 0;
    PagedList<Release> pagedList;
    Set<String> sensitiveGflags = new HashSet<>();
    do {
      pagedList =
          Release.find
              .query()
              .setFirstRow(pageIndex++ * pageSize)
              .setMaxRows(pageSize)
              .findPagedList();
      List<Release> releaseList = pagedList.getList();
      releaseList.parallelStream()
          .forEach(
              release -> {
                if (release.getSensitiveGflags() == null) {
                  release.setSensitiveGflags(
                      gFlagsValidation.getSensitiveJsonPathsForVersion(release.getVersion()));
                  release.save();
                }
              });

      for (Release r : releaseList) {
        sensitiveGflags.addAll(r.getSensitiveGflags());
      }
    } while (pagedList.hasNext());
    RedactingService.SECRET_JSON_PATHS_LOGS.addAll(
        sensitiveGflags.stream().map(JsonPath::compile).collect(Collectors.toList()));
  }
}
