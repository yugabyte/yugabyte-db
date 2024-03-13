/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.ha;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.ConfigException;
import com.typesafe.config.ConfigValue;
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.ha.PlatformReplicationManager.PlatformBackupParams;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.net.URI;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.apache.pekko.actor.Cancellable;
import org.apache.velocity.Template;
import org.apache.velocity.VelocityContext;
import org.apache.velocity.app.VelocityEngine;
import org.apache.velocity.runtime.RuntimeConstants;
import org.apache.velocity.runtime.resource.loader.ClasspathResourceLoader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@Singleton
public class PlatformReplicationHelper {
  private static final Logger LOG = LoggerFactory.getLogger(PlatformReplicationHelper.class);

  public static final String BACKUP_DIR = "platformBackups";
  public static final String REPLICATION_DIR = "platformReplication";
  static final String BACKUP_FILE_PATTERN = "backup_*.tgz";

  // Config keys:
  private static final String REPLICATION_SCHEDULE_ENABLED_KEY =
      "yb.ha.replication_schedule_enabled";
  private static final String NUM_BACKUP_RETENTION_KEY = "yb.ha.num_backup_retention";
  static final String REPLICATION_FREQUENCY_KEY = "yb.ha.replication_frequency";
  static final String DB_USERNAME_CONFIG_KEY = "db.default.username";
  static final String DB_PASSWORD_CONFIG_KEY = "db.default.password";
  static final String DB_HOST_CONFIG_KEY = "db.default.host";
  static final String DB_PORT_CONFIG_KEY = "db.default.port";
  static final String YBA_INSTALLATION_KEY = "yb.installation";
  public static final String WS_ACCEPT_ANY_CERTIFICATE_KEY =
      "play.ws.ssl.loose.acceptAnyCertificate";
  static final String WS_TIMEOUT_REQUEST_KEY = "play.ws.timeout.request";
  static final String WS_TIMEOUT_CONNECTION_KEY = "play.ws.timeout.connection";

  private final RuntimeConfGetter confGetter;

  private final SettableRuntimeConfigFactory runtimeConfigFactory;

  private final ApiHelper apiHelper;

  private final PlatformInstanceClientFactory remoteClientFactory;

  private final MetricUrlProvider metricUrlProvider;

  private final PrometheusConfigHelper prometheusConfigHelper;

  private final PrometheusConfigManager prometheusConfigManager;

  @VisibleForTesting ShellProcessHandler shellProcessHandler;

  @Inject
  public PlatformReplicationHelper(
      RuntimeConfGetter confGetter,
      SettableRuntimeConfigFactory runtimeConfigFactory,
      ApiHelper apiHelper,
      PlatformInstanceClientFactory remoteClientFactory,
      ShellProcessHandler shellProcessHandler,
      MetricUrlProvider metricUrlProvider,
      PrometheusConfigHelper prometheusConfigHelper,
      PrometheusConfigManager prometheusConfigManager) {
    this.confGetter = confGetter;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.apiHelper = apiHelper;
    this.remoteClientFactory = remoteClientFactory;
    this.shellProcessHandler = shellProcessHandler;
    this.metricUrlProvider = metricUrlProvider;
    this.prometheusConfigHelper = prometheusConfigHelper;
    this.prometheusConfigManager = prometheusConfigManager;
  }

  Path getBackupDir() {
    return Paths.get(
            confGetter.getStaticConf().getString(AppConfigHelper.YB_STORAGE_PATH), BACKUP_DIR)
        .toAbsolutePath();
  }

  int getNumBackupsRetention() {
    return Math.max(0, confGetter.getStaticConf().getInt(NUM_BACKUP_RETENTION_KEY));
  }

  String getDBUser() {
    return confGetter.getStaticConf().getString(DB_USERNAME_CONFIG_KEY);
  }

  String getDBPassword() {
    return confGetter.getStaticConf().getString(DB_PASSWORD_CONFIG_KEY);
  }

  String getDBHost() {
    return confGetter.getStaticConf().getString(DB_HOST_CONFIG_KEY);
  }

  int getDBPort() {
    return confGetter.getStaticConf().getInt(DB_PORT_CONFIG_KEY);
  }

  Duration getTestConnectionRequestTimeout() {
    return confGetter.getGlobalConf(GlobalConfKeys.haTestConnectionRequestTimeout);
  }

  Duration getTestConnectionConnectionTimeout() {
    return confGetter.getGlobalConf(GlobalConfKeys.haTestConnectionConnectionTimeout);
  }

  String getPGDumpPath() {
    try {
      return confGetter.getGlobalConf(GlobalConfKeys.pgDumpPath);
    } catch (ConfigException e) {
      throw new RuntimeException("Could not find pg_dump path.");
    }
  }

  String getPGRestorePath() {
    try {
      return confGetter.getGlobalConf(GlobalConfKeys.pgRestorePath);
    } catch (ConfigException e) {
      throw new RuntimeException("Could not find pg_restore path.");
    }
  }

  String getInstallationType() {
    try {
      return confGetter.getStaticConf().getString(YBA_INSTALLATION_KEY);
    } catch (ConfigException e) {
      return "";
    }
  }

  String getBaseInstall() {
    return Paths.get(confGetter.getStaticConf().getString(AppConfigHelper.YB_STORAGE_PATH))
        .getParent()
        .getParent()
        .toString();
  }

  boolean isBackupScheduleEnabled() {
    return runtimeConfigFactory.globalRuntimeConf().getBoolean(REPLICATION_SCHEDULE_ENABLED_KEY);
  }

  void setBackupScheduleEnabled(boolean enabled) {
    runtimeConfigFactory
        .globalRuntimeConf()
        .setValue(REPLICATION_SCHEDULE_ENABLED_KEY, Boolean.toString(enabled));
  }

  boolean isBackupScheduleRunning(Cancellable schedule) {
    return schedule != null && !schedule.isCancelled();
  }

  boolean isBackupScriptOutputEnabled() {
    return confGetter.getGlobalConf(GlobalConfKeys.logScriptOutput);
  }

  Duration getBackupFrequency() {
    return runtimeConfigFactory.globalRuntimeConf().getDuration(REPLICATION_FREQUENCY_KEY);
  }

  public void setReplicationFrequency(Duration duration) {
    runtimeConfigFactory
        .globalRuntimeConf()
        .setValue(REPLICATION_FREQUENCY_KEY, String.format("%d ms", duration.toMillis()));
  }

  JsonNode getBackupInfoJson(long frequency, boolean isRunning) {
    return Json.newObject().put("frequency_milliseconds", frequency).put("is_running", isRunning);
  }

  Path getReplicationDirFor(String leader) {
    String storagePath = confGetter.getStaticConf().getString(AppConfigHelper.YB_STORAGE_PATH);
    return Paths.get(storagePath, REPLICATION_DIR, leader);
  }

  private void writeFederatedPrometheusConfig(String remoteAddr, File file, boolean https) {
    try (BufferedWriter writer = new BufferedWriter(new FileWriter(file))) {
      VelocityEngine velocityEngine = new VelocityEngine();
      velocityEngine.setProperty(RuntimeConstants.RESOURCE_LOADER, "classpath");
      velocityEngine.setProperty(
          "classpath.resource.loader.class", ClasspathResourceLoader.class.getName());
      velocityEngine.init();

      // Load the template.
      Template template = velocityEngine.getTemplate("federated_prometheus.vm");

      // Fill in the context.
      VelocityContext context = new VelocityContext();
      context.put("interval", SwamperHelper.getScrapeIntervalSeconds(confGetter.getStaticConf()));
      context.put("address", remoteAddr);
      context.put("https", https);
      context.put("auth", confGetter.getGlobalConf(GlobalConfKeys.metricsAuth));
      context.put("username", confGetter.getGlobalConf(GlobalConfKeys.metricsAuthUsername));
      context.put("password", confGetter.getGlobalConf(GlobalConfKeys.metricsAuthPassword));

      // Merge the template with the context.
      template.merge(context, writer);
    } catch (Exception e) {
      LOG.error("Error creating federated prometheus config file");
    }
  }

  boolean demoteRemoteInstance(PlatformInstance remoteInstance, boolean promote) {
    try {
      if (remoteInstance.getIsLocal()) {
        LOG.warn("Cannot perform demoteRemoteInstance action on a local instance");
        return false;
      }

      HighAvailabilityConfig config = remoteInstance.getConfig();
      PlatformInstanceClient client =
          this.remoteClientFactory.getClient(
              config.getClusterKey(),
              remoteInstance.getAddress(),
              config.getAcceptAnyCertificateOverrides());

      // Ensure all local records for remote instances are set to follower state.
      remoteInstance.demote();

      return config
          .getLocal()
          .map(
              localInstance -> {
                // Send step down request to remote instance.
                client.demoteInstance(
                    localInstance.getAddress(), config.getLastFailover().getTime(), promote);

                return true;
              })
          .orElse(false);
    } catch (Exception e) {
      LOG.error("Error demoting remote platform instance {}", remoteInstance.getAddress(), e);
    }

    return false;
  }

  boolean exportPlatformInstances(HighAvailabilityConfig config, String remoteInstanceAddr) {
    try {
      PlatformInstanceClient client =
          this.remoteClientFactory.getClient(
              config.getClusterKey(),
              remoteInstanceAddr,
              config.getAcceptAnyCertificateOverrides());

      // Form payload to send to remote platform instance.
      List<PlatformInstance> instances = config.getInstances();
      JsonNode instancesJson = Json.toJson(instances);

      // Export the platform instances to the given remote platform instance.
      client.syncInstances(config.getLastFailover().getTime(), instancesJson);
      return true;
    } catch (Exception e) {
      LOG.error(
          "Error exporting local platform instances to remote instance " + remoteInstanceAddr, e);
    }
    return false;
  }

  void switchPrometheusToFederated(URL remoteAddr) {
    try {
      File configFile = prometheusConfigHelper.getPrometheusConfigFile();
      File configDir = configFile.getParentFile();
      File previousConfigFile = new File(configDir, "previous_prometheus.yml");

      if (!configDir.exists() && !configDir.mkdirs()) {
        LOG.warn("Could not create output dir {}", configDir);
        return;
      }

      // Move the old file if it hasn't already been moved.
      if (configFile.exists() && !previousConfigFile.exists()) {
        FileUtils.moveFile(configFile.toPath(), previousConfigFile.toPath());
      }

      // Write the filled in template to disk.
      // TBD: Need to fetch the Prometheus port from the remote PlatformInstance and use that here.
      // For now, we assume that the remote instance also uses the same port as the local one.
      String federatedAddr = metricUrlProvider.getMetricsExternalUrl();

      URI federatedURL = new URI(federatedAddr);
      String federatedPoint = remoteAddr.getHost() + ":" + federatedURL.getPort();
      boolean https = federatedURL.getScheme().equalsIgnoreCase("https");
      this.writeFederatedPrometheusConfig(federatedPoint, configFile, https);

      // Reload the config.
      prometheusConfigHelper.reloadPrometheusConfig();
    } catch (Exception e) {
      LOG.error("Error switching prometheus config to read from {}", remoteAddr.getHost(), e);
    }
  }

  void switchPrometheusToStandalone() {
    try {
      File configFile = prometheusConfigHelper.getPrometheusConfigFile();
      File configDir = configFile.getParentFile();
      File previousConfigFile = new File(configDir, "previous_prometheus.yml");

      if (!previousConfigFile.exists()) {
        throw new RuntimeException("Previous prometheus config file could not be found");
      }

      FileUtils.moveFile(previousConfigFile.toPath(), configFile.toPath());
      prometheusConfigHelper.reloadPrometheusConfig();
      prometheusConfigManager.updateK8sScrapeConfigs();
    } catch (Exception e) {
      LOG.error("Error switching prometheus config to standalone", e);
    }
  }

  void ensurePrometheusConfig() {
    HighAvailabilityConfig.get()
        .ifPresent(
            haConfig ->
                haConfig
                    .getLocal()
                    .ifPresent(
                        localInstance -> {
                          if (!localInstance.getIsLeader()) {
                            haConfig
                                .getLeader()
                                .ifPresent(
                                    leaderInstance -> {
                                      try {
                                        this.switchPrometheusToFederated(
                                            new URL(leaderInstance.getAddress()));
                                      } catch (Exception ignored) {
                                      }
                                    });
                          } else {
                            this.switchPrometheusToStandalone();
                          }
                        }));
  }

  boolean exportBackups(
      HighAvailabilityConfig config,
      String clusterKey,
      String remoteInstanceAddr,
      File backupFile) {
    Optional<PlatformInstance> localInstance = config.getLocal();
    Optional<PlatformInstance> leaderInstance = config.getLeader();
    return localInstance.isPresent()
        && leaderInstance.isPresent()
        && remoteClientFactory
            .getClient(clusterKey, remoteInstanceAddr, config.getAcceptAnyCertificateOverrides())
            .syncBackups(
                leaderInstance.get().getAddress(),
                localInstance.get().getAddress(), // sender is same as leader for now.
                backupFile);
  }

  boolean testConnection(
      HighAvailabilityConfig config,
      String clusterKey,
      String remoteInstanceAddr,
      boolean acceptAnyCertificate) {
    Optional<PlatformInstance> localInstance = config.getLocal();
    Optional<PlatformInstance> leaderInstance = config.getLeader();
    Map<String, ConfigValue> ybWsOverrides =
        new HashMap<>(
            Map.of(
                WS_ACCEPT_ANY_CERTIFICATE_KEY,
                ConfigValueFactory.fromAnyRef(acceptAnyCertificate)));
    ybWsOverrides.put(
        WS_TIMEOUT_REQUEST_KEY,
        ConfigValueFactory.fromAnyRef(
            String.format("%d seconds", getTestConnectionRequestTimeout().getSeconds())));
    ybWsOverrides.put(
        WS_TIMEOUT_CONNECTION_KEY,
        ConfigValueFactory.fromAnyRef(
            String.format("%d seconds", getTestConnectionConnectionTimeout().getSeconds())));
    return localInstance.isPresent()
        && leaderInstance.isPresent()
        && remoteClientFactory
            .getClient(clusterKey, remoteInstanceAddr, ybWsOverrides)
            .testConnection();
  }

  void cleanupBackups(List<File> backups, int numToRetain) {
    int numBackups = backups.size();

    if (numBackups <= numToRetain) {
      return;
    }

    LOG.info("Garbage collecting {} backups", numBackups - numToRetain);
    backups.subList(0, numBackups - numToRetain).forEach(File::delete);
  }

  Optional<File> getMostRecentBackup() {
    try {
      return Optional.of(FileUtils.listFiles(this.getBackupDir(), BACKUP_FILE_PATTERN).get(0));
    } catch (Exception exception) {
      LOG.error("Could not locate recent backup", exception);
    }

    return Optional.empty();
  }

  void cleanupCreatedBackups() {
    try {
      List<File> backups = FileUtils.listFiles(this.getBackupDir(), BACKUP_FILE_PATTERN);
      this.cleanupBackups(backups, 0);
    } catch (IOException ioException) {
      LOG.warn("Failed to list or delete backups");
    }
  }

  boolean syncToRemoteInstance(PlatformInstance remoteInstance) {
    HighAvailabilityConfig config = remoteInstance.getConfig();
    String remoteAddr = remoteInstance.getAddress();
    LOG.debug("Syncing data to " + remoteAddr + "...");

    // Ensure that the remote instance is demoted if this instance is the most current leader.
    if (!this.demoteRemoteInstance(remoteInstance, false)) {
      LOG.error("Error demoting remote instance " + remoteAddr);
      return false;
    }

    // Sync the HA cluster metadata to the remote instance.
    return this.exportPlatformInstances(config, remoteAddr);
  }

  List<File> listBackups(URL leader) {
    try {
      Path backupDir = this.getReplicationDirFor(leader.getHost());

      if (!backupDir.toFile().exists() || !backupDir.toFile().isDirectory()) {
        LOG.debug(String.format("%s directory does not exist", backupDir.toFile().getName()));

        return new ArrayList<>();
      }

      return FileUtils.listFiles(backupDir, PlatformReplicationHelper.BACKUP_FILE_PATTERN);
    } catch (Exception e) {
      LOG.error("Error listing backups for platform instance {}", leader.getHost(), e);

      return new ArrayList<>();
    }
  }

  void cleanupReceivedBackups(URL leader, int numToRetain) {
    List<File> backups = this.listBackups(leader);
    this.cleanupBackups(backups, numToRetain);
  }

  Optional<PlatformInstance> processImportedInstance(PlatformInstance i) {
    Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get();
    Optional<PlatformInstance> existingInstance = PlatformInstance.getByAddress(i.getAddress());
    if (config.isPresent()) {
      // Ensure the previous leader is marked as a follower to avoid uniqueness violation.
      if (i.getIsLeader()) {
        Optional<PlatformInstance> existingLeader = config.get().getLeader();
        if (existingLeader.isPresent()
            && !existingLeader.get().getAddress().equals(i.getAddress())) {
          existingLeader.get().demote();
        }
      }

      if (existingInstance.isPresent()) {
        // Since we sync instances after sending backups, the leader instance has the source of
        // truth as to when the last backup has been successfully sent to followers.
        existingInstance.get().setLastBackup(i.getLastBackup());
        existingInstance.get().setIsLeader(i.getIsLeader());
        existingInstance.get().update();
        i = existingInstance.get();
      } else {
        i.setIsLocal(false);
        i.setConfig(config.get());
        i.save();
      }

      return Optional.of(i);
    }

    return Optional.empty();
  }

  synchronized <T extends PlatformBackupParams> ShellResponse runCommand(T params) {
    List<String> commandArgs = params.getCommandArgs();
    Map<String, String> extraVars = params.getExtraVars();

    LOG.debug("Command to run: [" + String.join(" ", commandArgs) + "]");

    boolean logCmdOutput = isBackupScriptOutputEnabled();
    return shellProcessHandler.run(commandArgs, extraVars, logCmdOutput);
  }
}
