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

import akka.actor.Cancellable;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformInstanceClient;
import com.yugabyte.yw.common.PlatformInstanceClientFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import io.ebean.Model;
import org.apache.velocity.Template;
import org.apache.velocity.VelocityContext;
import org.apache.velocity.app.VelocityEngine;
import org.apache.velocity.runtime.RuntimeConstants;
import org.apache.velocity.runtime.resource.loader.ClasspathResourceLoader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.List;

@Singleton
public class PlatformReplicationHelper {
  private static final Logger LOG = LoggerFactory.getLogger(PlatformReplicationHelper.class);

  public static final String BACKUP_DIR = "platformBackups";
  public static final String REPLICATION_DIR = "platformReplication";
  private static final String PROMETHEUS_CONFIG_FILENAME = "prometheus.yml";

  // Config keys:
  public static final String STORAGE_PATH_KEY = "yb.storage.path";
  private static final String REPLICATION_SCHEDULE_ENABLED_KEY =
    "yb.ha.replication_schedule_enabled";
  private static final String PROMETHEUS_FEDERATED_CONFIG_DIR_KEY = "yb.ha.prometheus_config_dir";
  private static final String NUM_BACKUP_RETENTION_KEY = "yb.ha.num_backup_retention";
  static final String PROMETHEUS_HOST_CONFIG_KEY = "yb.metrics.host";
  static final String REPLICATION_FREQUENCY_KEY = "yb.ha.replication_frequency";
  static final String DB_USERNAME_CONFIG_KEY = "db.default.username";
  static final String DB_PASSWORD_CONFIG_KEY = "db.default.password";
  static final String DB_HOST_CONFIG_KEY = "db.default.host";
  static final String DB_PORT_CONFIG_KEY = "db.default.port";

  private final SettableRuntimeConfigFactory runtimeConfigFactory;

  private final ApiHelper apiHelper;

  private final PlatformInstanceClientFactory remoteClientFactory;

  @Inject
  public PlatformReplicationHelper(
    SettableRuntimeConfigFactory runtimeConfigFactory,
    ApiHelper apiHelper,
    PlatformInstanceClientFactory remoteClientFactory
  ) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.apiHelper = apiHelper;
    this.remoteClientFactory = remoteClientFactory;
  }

  RuntimeConfig<Model> getRuntimeConfig() {
    return this.runtimeConfigFactory.globalRuntimeConf();
  }

  Path getBackupDir() {
    return Paths.get(
      this.getRuntimeConfig().getString(STORAGE_PATH_KEY),
      BACKUP_DIR
    ).toAbsolutePath();
  }

  String getPrometheusHost() {
    return this.getRuntimeConfig().getString(PROMETHEUS_HOST_CONFIG_KEY);
  }

  int getNumBackupsRetention() {
    return Math.max(0, this.getRuntimeConfig().getInt(NUM_BACKUP_RETENTION_KEY));
  }

  String getDBUser() {
    return this.getRuntimeConfig().getString(DB_USERNAME_CONFIG_KEY);
  }

  String getDBPassword() {
    return this.getRuntimeConfig().getString(DB_PASSWORD_CONFIG_KEY);
  }

  String getDBHost() {
    return this.getRuntimeConfig().getString(DB_HOST_CONFIG_KEY);
  }

  int getDBPort() {
    return this.getRuntimeConfig().getInt(DB_PORT_CONFIG_KEY);
  }

  boolean isBackupScheduleEnabled() {
    return this.getRuntimeConfig().getBoolean(REPLICATION_SCHEDULE_ENABLED_KEY);
  }

  void setBackupScheduleEnabled(boolean enabled) {
    this.getRuntimeConfig().setValue(
      REPLICATION_SCHEDULE_ENABLED_KEY,
      Boolean.toString(enabled)
    );
  }

  boolean isBackupScheduleRunning(Cancellable schedule) {
    return schedule != null && !schedule.isCancelled();
  }

  private File getPrometheusConfigDir() {
    String outputDirString = this.getRuntimeConfig().getString(PROMETHEUS_FEDERATED_CONFIG_DIR_KEY);

    return new File(outputDirString);
  }

  private File getPrometheusConfigFile() {
    File configDir = getPrometheusConfigDir();

    return new File(configDir, PROMETHEUS_CONFIG_FILENAME);
  }

  Duration getBackupFrequency() {
    return this.getRuntimeConfig().getDuration(REPLICATION_FREQUENCY_KEY);
  }

  JsonNode getBackupInfoJson(long frequency, boolean isRunning) {
    return Json.newObject()
      .put("frequency_milliseconds", frequency)
      .put("is_running", isRunning);
  }

  Path getReplicationDirFor(String leader) {
    String storagePath = this.getRuntimeConfig().getString(STORAGE_PATH_KEY);
    return Paths.get(storagePath, REPLICATION_DIR, leader);
  }

  private void writeFederatedPrometheusConfig(String remoteAddr, File file) {
    try (BufferedWriter writer = new BufferedWriter(new FileWriter(file))) {
      VelocityEngine velocityEngine = new VelocityEngine();
      velocityEngine.setProperty(RuntimeConstants.RESOURCE_LOADER, "classpath");
      velocityEngine.setProperty(
        "classpath.resource.loader.class",
        ClasspathResourceLoader.class.getName()
      );
      velocityEngine.init();

      // Load the template.
      Template template = velocityEngine.getTemplate("federated_prometheus.vm");

      // Fill in the context.
      VelocityContext context = new VelocityContext();
      context.put("address", remoteAddr);

      // Merge the template with the context.
      template.merge(context, writer);
    } catch (Exception e) {
      LOG.error("Error creating federated prometheus config file");
    }
  }

  private void reloadPrometheusConfig() {
    try {
      String localPromHost = this.getRuntimeConfig().getString(PROMETHEUS_HOST_CONFIG_KEY);
      URL reloadEndpoint = new URL("http", localPromHost, 9090, "/-/reload");

      // Send the reload request.
      this.apiHelper.postRequest(reloadEndpoint.toString(), Json.newObject());
    } catch (Exception e) {
      LOG.error("Error reloading prometheus config", e);
    }
  }

  boolean demoteRemoteInstance(PlatformInstance remoteInstance) {
    try {
      if (remoteInstance.getIsLocal()) {
        throw new RuntimeException("Cannot perform this action on a local instance");
      }

      HighAvailabilityConfig config = remoteInstance.getConfig();
      PlatformInstanceClient client = this.remoteClientFactory.getClient(
        config.getClusterKey(),
        remoteInstance.getAddress()
      );

      // Ensure all local records for remote instances are set to follower state.
      remoteInstance.demote();

      // Send step down request to remote instance.
      client.demoteInstance(config.getLocal().getAddress(), config.getLastFailover().getTime());

      return true;
    } catch (Exception e) {
      LOG.error("Error demoting remote platform instance {}", remoteInstance.getAddress(), e);
    }

    return false;
  }

  void exportPlatformInstances(HighAvailabilityConfig config, String remoteInstanceAddr) {
    try {
      PlatformInstanceClient client = this.remoteClientFactory.getClient(
        config.getClusterKey(),
        remoteInstanceAddr
      );

      // Form payload to send to remote platform instance.
      List<PlatformInstance> instances = config.getInstances();
      JsonNode instancesJson = Json.toJson(instances);

      // Export the platform instances to the given remote platform instance.
      client.syncInstances(config.getLastFailover().getTime(), instancesJson);
    } catch (Exception e) {
      LOG.error("Error exporting local platform instances to remote instance "
        + remoteInstanceAddr, e);
    }
  }

  void switchPrometheusToFederated(URL remoteAddr) {
    try {
      File configFile = this.getPrometheusConfigFile();
      File configDir = configFile.getParentFile();
      File previousConfigFile = new File(configDir, "previous_prometheus.yml");

      if (!configDir.exists() && !configDir.mkdirs()) {
        LOG.warn("Could not create output dir");

        return;
      }

      // Move the old file if it hasn't already been moved.
      if (configFile.exists() && !previousConfigFile.exists()) {
        Util.moveFile(configFile.toPath(), previousConfigFile.toPath());
      }

      // Write the filled in template to disk.
      String federatedAddr = remoteAddr.getHost() + ":" + 9090;
      this.writeFederatedPrometheusConfig(federatedAddr, configFile);

      // Reload the config.
      this.reloadPrometheusConfig();
    } catch (Exception e) {
      LOG.error("Error switching prometheus config to read from {}", remoteAddr.getHost(), e);
    }
  }

  void switchPrometheusToStandalone() throws Exception {
    File configFile = this.getPrometheusConfigFile();
    File configDir = configFile.getParentFile();
    File previousConfigFile = new File(configDir, "previous_prometheus.yml");

    if (!previousConfigFile.exists()) {
      throw new RuntimeException("Previous prometheus config file could not be found");
    }

    Util.moveFile(previousConfigFile.toPath(), configFile.toPath());
    this.reloadPrometheusConfig();
  }

  void ensurePrometheusConfig() {
    try {
      HighAvailabilityConfig haConfig = HighAvailabilityConfig.list().get(0);
      PlatformInstance localInstance = haConfig.getLocal();
      if (!localInstance.getIsLeader()) {
        URL leaderAddr = new URL(haConfig.getLeader().getAddress());
        this.switchPrometheusToFederated(leaderAddr);
      } else {
        this.switchPrometheusToStandalone();
      }
    } catch (Exception ignored) {
    }
  }

  boolean exportBackups(HighAvailabilityConfig config, String clusterKey,
                        String remoteInstanceAddr, File backupFile) {
    return remoteClientFactory.getClient(clusterKey, remoteInstanceAddr).syncBackups(
      config.getLeader().getAddress(),
      config.getLocal().getAddress(), // sender is same as leader for now.
      backupFile);
  }

  void cleanupBackups(List<File> backups, int numToRetain) {
    int numBackups = backups.size();

    if (numBackups <= numToRetain) {
      return;
    }

    LOG.debug("Garbage collecting {} backups", numBackups - numToRetain);
    backups.subList(0, numBackups - numToRetain).forEach(File::delete);
  }
}
