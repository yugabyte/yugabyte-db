/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
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
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PrometheusConfigHelper;
import com.yugabyte.yw.common.PrometheusConfigManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.ha.PlatformReplicationManager.PlatformBackupParams;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfigurationThreshold;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.net.URI;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.pekko.actor.Cancellable;
import org.apache.velocity.Template;
import org.apache.velocity.VelocityContext;
import org.apache.velocity.app.VelocityEngine;
import org.apache.velocity.runtime.RuntimeConstants;
import org.apache.velocity.runtime.resource.loader.ClasspathResourceLoader;
import play.libs.Json;

@Singleton
@Slf4j
public class PlatformReplicationHelper {

  public static final String BACKUP_DIR = "platformBackups";
  public static final String REPLICATION_DIR = "platformReplication";
  static final String BACKUP_FILE_PATTERN = "backup_*.tgz";
  static final String LOCAL_HA_CONFIG_JSON_FILE = "local_ha_config.json";

  // Config keys:
  private static final String REPLICATION_SCHEDULE_ENABLED_KEY =
      "yb.ha.replication_schedule_enabled";
  private static final String NUM_BACKUP_RETENTION_KEY = "yb.ha.num_backup_retention";
  public static final String REPLICATION_FREQUENCY_KEY = "yb.ha.replication_frequency";
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

  private final PlatformInstanceClientFactory remoteClientFactory;

  private final MetricUrlProvider metricUrlProvider;

  private final PrometheusConfigHelper prometheusConfigHelper;

  private final PrometheusConfigManager prometheusConfigManager;

  private final AlertConfigurationService alertConfigurationService;

  @VisibleForTesting MetricQueryHelper metricQueryHelper;

  @VisibleForTesting ShellProcessHandler shellProcessHandler;

  @Inject
  public PlatformReplicationHelper(
      RuntimeConfGetter confGetter,
      SettableRuntimeConfigFactory runtimeConfigFactory,
      PlatformInstanceClientFactory remoteClientFactory,
      ShellProcessHandler shellProcessHandler,
      MetricUrlProvider metricUrlProvider,
      PrometheusConfigHelper prometheusConfigHelper,
      PrometheusConfigManager prometheusConfigManager,
      AlertConfigurationService alertConfigurationService,
      MetricQueryHelper metricQueryHelper) {
    this.confGetter = confGetter;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.remoteClientFactory = remoteClientFactory;
    this.shellProcessHandler = shellProcessHandler;
    this.metricUrlProvider = metricUrlProvider;
    this.prometheusConfigHelper = prometheusConfigHelper;
    this.prometheusConfigManager = prometheusConfigManager;
    this.alertConfigurationService = alertConfigurationService;
    this.metricQueryHelper = metricQueryHelper;
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
    List<AlertConfiguration> haStandbyAlertConfigs =
        alertConfigurationService.list(
            AlertConfigurationFilter.builder().template(AlertTemplate.HA_STANDBY_SYNC).build());
    haStandbyAlertConfigs.forEach(
        haStandbyAlertConfig -> {
          haStandbyAlertConfig.setThresholds(
              haStandbyAlertConfig.getThresholds().entrySet().stream()
                  .collect(
                      Collectors.toMap(
                          Map.Entry::getKey,
                          e ->
                              new AlertConfigurationThreshold()
                                  .setCondition(e.getValue().getCondition())
                                  .setThreshold(
                                      Math.max(
                                          // Convert to minutes
                                          2 * duration.toMillis() / 1000.0 / 60.0,
                                          e.getValue().getThreshold())))));
          alertConfigurationService.save(haStandbyAlertConfig);
        });
  }

  JsonNode getBackupInfoJson(long frequency, boolean isRunning) {
    return Json.newObject().put("frequency_milliseconds", frequency).put("is_running", isRunning);
  }

  public Path getReplicationDirFor(String leader) {
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
      context.put(
          "interval",
          SwamperHelper.getScrapeIntervalSeconds(
              confGetter.getGlobalConf(GlobalConfKeys.metricScrapeIntervalStandby)));
      context.put("address", remoteAddr);
      context.put("https", https);
      context.put("auth", confGetter.getGlobalConf(GlobalConfKeys.metricsAuth));
      context.put("username", confGetter.getGlobalConf(GlobalConfKeys.metricsAuthUsername));
      context.put("password", confGetter.getGlobalConf(GlobalConfKeys.metricsAuthPassword));

      // Merge the template with the context.
      template.merge(context, writer);
    } catch (Exception e) {
      log.error("Error creating federated prometheus config file");
    }
  }

  // This makes calls to the remote instances to demote.
  boolean demoteRemoteInstance(PlatformInstance remoteInstance, boolean promote) {
    if (remoteInstance.isLocal()) {
      log.warn("Cannot perform demoteRemoteInstance action on a local instance");
      return false;
    }
    boolean succeeded = false;
    HighAvailabilityConfig config = remoteInstance.getConfig();
    try (PlatformInstanceClient client =
        this.remoteClientFactory.getClient(
            config.getClusterKey(),
            remoteInstance.getAddress(),
            config.getAcceptAnyCertificateOverrides())) {
      succeeded =
          config
              .getLocal()
              .map(
                  localInstance -> {
                    // Send step down request to remote instance.
                    log.info(
                        "Demoting remote instance {} in favor of {}",
                        remoteInstance.getAddress(),
                        localInstance.getAddress());
                    return client.demoteInstance(
                        localInstance.getAddress(), config.getLastFailover().getTime(), promote);
                  })
              .orElse(false);
      if (succeeded && remoteInstance.isLeader()) {
        // Ensure all local records for remote instances are set to follower state.
        remoteInstance.demote();
      }
    } catch (Exception e) {
      log.error("Error demoting remote platform instance {}", remoteInstance.getAddress(), e);
    }
    return succeeded;
  }

  public void clearMetrics(HighAvailabilityConfig config, String remoteInstanceAddr) {
    try (PlatformInstanceClient client =
        this.remoteClientFactory.getClient(
            config.getClusterKey(),
            remoteInstanceAddr,
            config.getAcceptAnyCertificateOverrides())) {
      client.clearMetrics();
    }
  }

  boolean exportPlatformInstances(HighAvailabilityConfig config, String remoteInstanceAddr) {
    try (PlatformInstanceClient client =
        this.remoteClientFactory.getClient(
            config.getClusterKey(),
            remoteInstanceAddr,
            config.getAcceptAnyCertificateOverrides())) {

      // Form payload to send to remote platform instance.
      List<PlatformInstance> instances = config.getInstances();
      JsonNode instancesJson = Json.toJson(instances);
      // Export the platform instances to the given remote platform instance.
      client.syncInstances(config.getLastFailover().getTime(), instancesJson);
      return true;
    } catch (Exception e) {
      log.error(
          "Error exporting local platform instances to remote instance " + remoteInstanceAddr, e);
    }
    return false;
  }

  void switchPrometheusToFederated(URL remoteAddr) {
    try {
      log.info("Switching local prometheus to federated or updating it");
      File configFile = prometheusConfigHelper.getPrometheusConfigFile();
      File configDir = configFile.getParentFile();
      File previousConfigFile = new File(configDir, "previous_prometheus.yml");

      if (!configDir.exists() && !configDir.mkdirs()) {
        log.warn("Could not create output dir {}", configDir);
        return;
      }

      // Move the old file if it hasn't already been moved.
      if (configFile.exists() && !previousConfigFile.exists()) {
        log.info("Creating previous_prometheus.yml from existing prometheus.yml");
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
      log.info("Wrote federated prometheus config.");

      // Reload the config.
      prometheusConfigHelper.reloadPrometheusConfig();
    } catch (Exception e) {
      log.error("Error switching prometheus config to read from {}", remoteAddr.getHost(), e);
    }
  }

  void switchPrometheusToStandalone() {
    try {
      log.info("Switching prometheus to standalone.");
      File configFile = prometheusConfigHelper.getPrometheusConfigFile();
      File previousConfigFile = new File(configFile.getParentFile(), "previous_prometheus.yml");
      if (previousConfigFile.exists()) {
        FileUtils.moveFile(previousConfigFile.toPath(), configFile.toPath());
        prometheusConfigHelper.reloadPrometheusConfig();
        prometheusConfigManager.updateK8sScrapeConfigs();
        log.info("Moved previous_prometheus.yml to prometheus.yml");
      } else {
        // Regular sync call to PlatformReplicationManager::ensurePrometheusConfig should fix it if
        // there is an issue.
        log.warn(
            "Previous prometheus config file does not exist. It may already be running in"
                + " standalone more");
      }
    } catch (Exception e) {
      log.error("Error switching prometheus config to standalone", e);
    }
  }

  // Invoked by MetricsScrapeIntervalStandbyListener event.
  public void ensurePrometheusConfig() {
    Optional<HighAvailabilityConfig> optional = HighAvailabilityConfig.get();
    if (optional.isEmpty()) {
      return;
    }
    // No outgoing remote call and this event must make the change.
    HighAvailabilityConfig.doWithLock(
        optional.get().getUuid(),
        config -> {
          config
              .getLocal()
              .ifPresent(
                  localInstance -> {
                    if (!localInstance.isLeader()) {
                      config
                          .getLeader()
                          .ifPresent(
                              leaderInstance -> {
                                log.debug("Switching prometheus to federated mode");
                                switchPrometheusToFederated(
                                    Util.toURL(leaderInstance.getAddress()));
                              });
                    } else {
                      log.debug("Switching prometheus to standalone mode");
                      switchPrometheusToStandalone();
                    }
                  });
          return null;
        });
  }

  boolean exportBackups(
      HighAvailabilityConfig config,
      String clusterKey,
      String remoteInstanceAddr,
      File backupFile) {
    Optional<PlatformInstance> localInstance = config.getLocal();
    Optional<PlatformInstance> leaderInstance = config.getLeader();
    try (PlatformInstanceClient client =
        remoteClientFactory.getClient(
            clusterKey, remoteInstanceAddr, config.getAcceptAnyCertificateOverrides())) {
      return localInstance.isPresent()
          && leaderInstance.isPresent()
          && client.syncBackups(
              leaderInstance.get().getAddress(),
              localInstance.get().getAddress(), // sender is same as leader for now.
              backupFile);
    }
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
    try (PlatformInstanceClient client =
        remoteClientFactory.getClient(clusterKey, remoteInstanceAddr, ybWsOverrides)) {
      return localInstance.isPresent() && leaderInstance.isPresent() && client.testConnection();
    }
  }

  void cleanupBackups(List<File> backups, int numToRetain) {
    int numBackups = backups.size();

    if (numBackups <= numToRetain) {
      return;
    }

    log.info("Garbage collecting {} backups", numBackups - numToRetain);
    backups.subList(0, numBackups - numToRetain).forEach(File::delete);
  }

  public Optional<File> getMostRecentBackup() {
    return getMostRecentBackup(this.getBackupDir());
  }

  public Optional<File> getMostRecentBackup(Path backupDir) {
    try {
      return FileUtils.listFiles(backupDir, BACKUP_FILE_PATTERN).stream()
          .max(Comparator.comparingLong(File::lastModified));
    } catch (Exception exception) {
      log.error("Could not locate recent backup", exception);
    }

    return Optional.empty();
  }

  public void cleanupCreatedBackups() {
    try {
      List<File> backups = FileUtils.listFiles(this.getBackupDir(), BACKUP_FILE_PATTERN);
      // Keep 3 most recent backups to avoid interference between continuous backups and HA
      this.cleanupBackups(backups, 3);
    } catch (IOException e) {
      log.warn("Failed to list or delete backups", e);
    }
  }

  boolean syncToRemoteInstance(PlatformInstance remoteInstance) {
    String remoteAddr = remoteInstance.getAddress();
    log.debug("Syncing data to {}...", remoteAddr);
    boolean succeeded =
        HighAvailabilityConfig.doWithTryLock(
                remoteInstance.getConfig().getUuid(),
                config -> {
                  // Ensure that the remote instance is demoted if this instance is the most current
                  // leader.
                  if (!demoteRemoteInstance(remoteInstance, false)) {
                    log.warn("Syncer failed to demote remote instance {}", remoteAddr);
                    return false;
                  }
                  // Sync the HA cluster metadata to the remote instance.
                  return exportPlatformInstances(config, remoteAddr);
                })
            .orElse(false);
    if (!succeeded) {
      log.warn("Sync failed to demote remote instance {}", remoteAddr);
    }
    return succeeded;
  }

  List<File> listBackups(URL leader) {
    try {
      Path backupDir = getReplicationDirFor(leader.getHost());

      if (!backupDir.toFile().exists() || !backupDir.toFile().isDirectory()) {
        log.debug("{} directory does not exist", backupDir.toFile().getName());
        return Collections.emptyList();
      }

      return FileUtils.listFiles(backupDir, PlatformReplicationHelper.BACKUP_FILE_PATTERN);
    } catch (Exception e) {
      log.error("Error listing backups for platform instance {}", leader.getHost(), e);
      return Collections.emptyList();
    }
  }

  // Save the HA config to a file before it is wiped out in backup restore.
  Path saveLocalHighAvailabilityConfig(HighAvailabilityConfig config) {
    Path localConfigDir = getReplicationDirFor("localhost");
    Path localHaConfigPath = getReplicationDirFor("localhost").resolve(LOCAL_HA_CONFIG_JSON_FILE);
    try {
      localConfigDir.toFile().mkdirs();
      File file = localConfigDir.resolve(LOCAL_HA_CONFIG_JSON_FILE).toFile();
      if (file.exists()) {
        file.delete();
      }
      Json.mapper().writeValue(file, config);
      return localHaConfigPath;
    } catch (Exception e) {
      log.error("Failed to write local HA config to file {}", localHaConfigPath, e);
      throw new RuntimeException(e);
    }
  }

  // Read the HA config from the file.
  Optional<HighAvailabilityConfig> maybeGetLocalHighAvailabilityConfig(String clusterKey) {
    File localHaConfigFile =
        getReplicationDirFor("localhost").resolve(LOCAL_HA_CONFIG_JSON_FILE).toFile();
    try {
      if (localHaConfigFile.exists() && localHaConfigFile.isFile()) {
        HighAvailabilityConfig config =
            Json.mapper().readValue(localHaConfigFile, HighAvailabilityConfig.class);
        if (clusterKey.equals(config.getClusterKey())) {
          return Optional.of(config);
        } else {
          log.warn("Cluster keys do not match");
        }
      }
    } catch (Exception e) {
      log.warn("Failed to read local HA config from file {}", localHaConfigFile, e);
    }
    return Optional.empty();
  }

  // Delete the local HA config file.
  boolean deleteLocalHighAvailabilityConfig() {
    Path localHaConfigPath = getReplicationDirFor("localhost").resolve(LOCAL_HA_CONFIG_JSON_FILE);
    try {
      return Files.deleteIfExists(localHaConfigPath);
    } catch (IOException e) {
      log.warn("Fail to delete the local HA config file {}", localHaConfigPath, e);
    }
    return false;
  }

  void cleanupReceivedBackups(URL leader, int numToRetain) {
    List<File> backups = listBackups(leader);
    cleanupBackups(backups, numToRetain);
  }

  public synchronized <T extends PlatformBackupParams> ShellResponse runCommand(T params) {
    List<String> commandArgs = params.getCommandArgs();
    Map<String, String> extraVars = params.getExtraVars();

    log.debug("Command to run: [" + String.join(" ", commandArgs) + "]");

    boolean logCmdOutput = isBackupScriptOutputEnabled();
    return shellProcessHandler.run(commandArgs, extraVars, logCmdOutput);
  }

  @VisibleForTesting
  List<NodeAgent> getLiveNodeAgents() {
    return NodeAgent.getAll().stream()
        .filter(NodeAgent::isActive)
        .filter(n -> n.getLastError() == null)
        .collect(Collectors.toList());
  }

  /**
   * Checks if there is any live node agent with version mismatch with YBA software version at the
   * given instant. Consider only live nodes to prevent blocking forever due to dead node agents.
   *
   * @param at the instant to check.
   * @return true if yes else false.
   */
  public boolean isLiveNodeAgentUpgradePendingAt(Instant at) {
    String ybaVersion = Util.getYbaVersion();
    Set<String> liveNodeAgentUuids = new HashSet<>();
    for (NodeAgent nodeAgent : getLiveNodeAgents()) {
      if (!Util.areYbVersionsEqual(ybaVersion, nodeAgent.getVersion(), true)) {
        log.warn(
            "Node agent {} version {} does not match with YBA software version {}",
            nodeAgent,
            nodeAgent.getVersion(),
            ybaVersion);
        return true;
      }
      liveNodeAgentUuids.add(nodeAgent.getUuid().toString());
    }
    // Adding 1 minute buffer to make sure the back is well within the range.
    // Backup time is the creation time, not the completion time. So, it should be within the range
    // even if the backup took a long time to complete.
    long minutes = ChronoUnit.MINUTES.between(at, Instant.now()) + 1;
    if (minutes > 0) {
      // Check if there was any upgrading pending during this window.
      String query =
          String.format("max_over_time(ybp_nodeagent_version_mismatch[%dm]) > 0", minutes);
      List<MetricQueryResponse.Entry> entries = metricQueryHelper.queryDirect(query);
      for (MetricQueryResponse.Entry entry : entries) {
        String nodeAgentUuid = entry.labels.get("node_agent_uuid");
        if (StringUtils.isBlank(nodeAgentUuid) || !liveNodeAgentUuids.contains(nodeAgentUuid)) {
          log.debug("Node agent {} was already updated before {}", nodeAgentUuid, at);
          continue;
        }
        log.warn(
            "Node agent {} had version mismatch with YBA software during last {} minutes",
            nodeAgentUuid,
            minutes);
        return true;
      }
    }
    return false;
  }

  /* Makes a remote call to validate the backup. */
  public boolean validateRemoteBackup(
      HighAvailabilityConfig config, String remoteInstanceAddr, String backupName) {
    try (PlatformInstanceClient client =
        this.remoteClientFactory.getClient(
            config.getClusterKey(),
            remoteInstanceAddr,
            config.getAcceptAnyCertificateOverrides())) {
      return client.validateRemoteBackupAt(backupName);
    }
  }
}
