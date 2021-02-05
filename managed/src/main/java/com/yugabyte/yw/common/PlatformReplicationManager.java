/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import akka.actor.ActorSystem;
import akka.actor.Cancellable;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import io.ebean.Model;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import scala.concurrent.ExecutionContext;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.*;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import java.util.stream.StreamSupport;

@Singleton
public class PlatformReplicationManager extends DevopsBase {
  static final String BACKUP_SCRIPT = "bin/yb_platform_backup.sh";
  static final String BACKUP_DIR = "platformBackups";
  public static final String REPLICATION_DIR = "platformReplication";
  static final String DB_PASSWORD_ENV_VAR_KEY = "PGPASSWORD";

  // Config keys:
  static final String PROMETHEUS_HOST_CONFIG_KEY = "yb.metrics.host";
  static final String DB_USERNAME_CONFIG_KEY = "db.default.username";
  static final String DB_PASSWORD_CONFIG_KEY = "db.default.password";
  static final String DB_HOST_CONFIG_KEY = "db.default.host";
  static final String DB_PORT_CONFIG_KEY = "db.default.port";
  static final String REPLICATION_FREQUENCY_KEY = "yb.ha.replication_frequency";
  static final String REPLICATION_SCHEDULE_ENABLED_KEY = "yb.ha.replication_schedule_enabled";
  public static final String STORAGE_PATH_KEY = "yb.storage.path";
  public static final String RETAIN_DATA_FROM_INVALID_SRC_KEY =
    "yb.ha.retain_data_from_invalid_src";

  private final AtomicReference<Cancellable> schedule;

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final SettableRuntimeConfigFactory runtimeConfigFactory;

  private final PlatformInstanceClientFactory remoteClientFactory;

  private static final Logger LOG = LoggerFactory.getLogger(PlatformReplicationManager.class);

  @Override
  protected String getCommandType() {
    return null;
  }

  @Inject
  public PlatformReplicationManager(
    ActorSystem actorSystem,
    ExecutionContext executionContext,
    ShellProcessHandler shellProcessHandler,
    SettableRuntimeConfigFactory runtimeConfigFactory,
    PlatformInstanceClientFactory remoteClientFactory
  ) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.shellProcessHandler = shellProcessHandler;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.remoteClientFactory = remoteClientFactory;
    this.schedule = new AtomicReference<>(null);
  }

  private Cancellable getSchedule() {
    return this.schedule.get();
  }

  public void start() {
    if (this.isBackupScheduleRunning()) {
      LOG.warn("Platform backup schedule is already started");
      return;
    }

    if (!this.isBackupScheduleEnabled()) {
      LOG.debug("Cannot start backup schedule because it is disabled");
      return;
    }

    Duration frequency = getBackupFrequency();

    if (!frequency.isNegative() && !frequency.isZero()) {
      this.setSchedule(frequency);
    }
  }

  public void stop() {
    if (!this.isBackupScheduleRunning()) {
      LOG.warn("Platform backup schedule is already stopped");
      return;
    }

    if (!this.getSchedule().cancel()) {
      LOG.warn("Unknown error occurred stopping platform backup schedule");
    }
  }

  public JsonNode stopAndDisable() {
    this.stop();
    this.setBackupScheduleEnabled(false);

    return this.getBackupInfo();
  }

  private Duration getBackupFrequency() {
    return this.runtimeConfigFactory.globalRuntimeConf().getDuration(REPLICATION_FREQUENCY_KEY);
  }

  public JsonNode setFrequencyStartAndEnable(Duration duration) {
    this.stop();
    this.runtimeConfigFactory.globalRuntimeConf().setValue(
      REPLICATION_FREQUENCY_KEY,
      String.format("%d ms", duration.toMillis())
    );
    this.setBackupScheduleEnabled(true);
    this.start();

    return this.getBackupInfo();
  }

  private boolean isBackupScheduleRunning() {
    return this.getSchedule() != null && !this.getSchedule().isCancelled();
  }

  private boolean isBackupScheduleEnabled() {
    return this.runtimeConfigFactory.globalRuntimeConf()
      .getBoolean(REPLICATION_SCHEDULE_ENABLED_KEY);
  }

  private void setBackupScheduleEnabled(boolean enabled) {
    this.runtimeConfigFactory.globalRuntimeConf().setValue(
      REPLICATION_SCHEDULE_ENABLED_KEY,
      Boolean.toString(enabled)
    );
  }

  private void setSchedule(Duration frequency) {
    LOG.info("Scheduling periodic platform backups every {}", frequency.toString());
    this.schedule.set(this.actorSystem.scheduler().schedule(
      Duration.ofMillis(0), // initialDelay
      frequency, // interval
      this::sync,
      this.executionContext
    ));
  }

  Path getBackupDir() {
    return Paths.get(
      this.runtimeConfigFactory.globalRuntimeConf().getString(STORAGE_PATH_KEY),
      BACKUP_DIR
    );
  }

  // TODO: (Daniel/Shashank) - https://github.com/yugabyte/yugabyte-db/issues/6961.
  public List<File> listBackups(String leader) throws Exception {
    Path backupDir = this.getReplicationDirFor(leader);

    if (!backupDir.toFile().exists() || !backupDir.toFile().isDirectory()) {
      LOG.debug(String.format("%s directory does not exist", backupDir.toFile().getName()));

      return new ArrayList<>();
    }

    return listFiles(backupDir, "backup_*.tgz");
  }

  private static List<File> listFiles(Path backupDir, String pattern) throws IOException {
    return StreamSupport.stream(
      Files.newDirectoryStream(backupDir, pattern).spliterator(), false)
      .map(Path::toFile)
      .collect(Collectors.toList());
  }

  public JsonNode getBackupInfo() {
    return Json.newObject()
      .put("frequency_milliseconds", this.getBackupFrequency().toMillis())
      .put("is_running", this.isBackupScheduleRunning());
  }

  private boolean demotePreviousInstance(PlatformInstance remoteInstance) {
    try {
      HighAvailabilityConfig config = remoteInstance.getConfig();
      PlatformInstanceClient client = this.remoteClientFactory.getClient(
        config.getClusterKey(),
        remoteInstance.getAddress()
      );

      // Ensure all local records for remote instances are set to follower state.
      remoteInstance.demote();

      // Send step down request to remote instance.
      client.demoteInstance(config.getLastFailover().getTime());

      return true;
    } catch (Exception e) {
      LOG.error("Error demoting remote platform instance {}", remoteInstance.getAddress(), e);
    }

    return false;
  }

  public void promoteInstance(PlatformInstance newLeader) {
    HighAvailabilityConfig config = newLeader.getConfig();
    // Update which instance should be local.
    config.getInstances()
      .forEach(i -> i.setIsLocalAndUpdate(i.getUUID().equals(newLeader.getUUID())));
    // Mark the failover timestamp.
    config.updateLastFailover();
    // Attempt to ensure all remote instances are in follower state.
    // Remotely demote any instance reporting to be a leader.
    config.getRemoteInstances().forEach(this::demotePreviousInstance);
    // Promote the new local leader.
    newLeader.promote();
  }

  /**
   * A method to import a list of platform instances received from the leader platform instance.
   * Assumption is that any platform instance existing locally but not provided in the payload has
   * been deleted on the leader, and thus should be deleted here too.
   *
   * @param config    the local HA Config model
   * @param instances the JSON payload received from the leader instance
   */
  public void importPlatformInstances(HighAvailabilityConfig config, ArrayNode instances) {
    List<PlatformInstance> existingInstances = config.getInstances();
    // Get list of existing addresses.
    Set<String> existingAddrs = existingInstances.stream()
      .map(PlatformInstance::getAddress)
      .collect(Collectors.toSet());

    // Map request JSON payload to list of platform instances.
    List<PlatformInstance> newInstances = StreamSupport.stream(instances.spliterator(), false)
      .map(obj -> Json.fromJson(obj, PlatformInstance.class))
      .filter(Objects::nonNull)
      .collect(Collectors.toList());

    // Get list of request payload addresses.
    Set<String> newAddrs = newInstances.stream()
      .map(PlatformInstance::getAddress)
      .collect(Collectors.toSet());

    // Delete any instances that exist locally but aren't included in the sync request.
    Set<String> instanceAddrsToDelete = Sets.difference(existingAddrs, newAddrs);
    existingInstances.stream()
      .filter(i -> instanceAddrsToDelete.contains(i.getAddress()))
      .forEach(PlatformInstance::delete);

    // Import the new instances, or update existing ones.
    newInstances.forEach(i -> {
      PlatformInstance existingInstance = PlatformInstance.getByAddress(i.getAddress());
      if (existingInstance != null) {
        // Since we sync instances after sending backups, the leader instance has the source of
        // truth as to when the last backup has been successfully sent to followers.
        existingInstance.setLastBackup(i.getLastBackup());
        existingInstance.setIsLeader(i.getIsLeader());
        existingInstance.update();
      } else {
        i.setIsLocal(false);
        i.setConfig(config);
        i.save();
      }
    });
  }

  private void exportPlatformInstances(HighAvailabilityConfig config, String remoteInstanceAddr) {
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

  // TODO: (Shashank) - Implement https://github.com/yugabyte/yugabyte-db/issues/6503.
  //  You'll want to run "updateLastBackup()" on each remote instance the backup has
  //  successfully been sent to (You'll probably want to do this on the leader instance so that
  //  the backup times get picked up by the followers through syncPlatformInstances).
  //  If you update the last backup timestamp directly on the followers, it will be overwritten
  //  on there by the leader platform.
  private boolean sendBackup(PlatformInstance remoteInstance) {
    return true;
  }

  private void syncToRemoteInstance(PlatformInstance remoteInstance) {
    HighAvailabilityConfig config = remoteInstance.getConfig();
    String remoteAddr = remoteInstance.getAddress();
    LOG.debug("Syncing data to " + remoteAddr + "...");

    // Ensure that the remote instance is demoted if this instance is the most current leader.
    if (!this.demotePreviousInstance(remoteInstance)) {
      LOG.error("Error demoting remote instance " + remoteAddr);

      return;
    }

    // Send the platform backup to the remote instance.
    if (!this.sendBackup(remoteInstance)) {
      LOG.error("Error sending platform backup to " + remoteAddr);

      return;
    }

    // Sync the HA cluster metadata to the remote instance.
    this.exportPlatformInstances(config, remoteAddr);
  }

  private void sync() {
    HighAvailabilityConfig.list().forEach(config -> {
      List<PlatformInstance> remoteInstances = config.getRemoteInstances();
      // No point in taking a backup if there is no one to send it to.
      if (remoteInstances.isEmpty()) {
        LOG.debug("Skipping HA cluster sync...");

        return;
      }

      // Create the platform backup.
      if (!this.createBackup()) {
        LOG.error("Error creating platform backup");

        return;
      }

      // Update local last backup time if creating the backup succeeded.
      config.getLocal().updateLastBackup();

      // Sync data to remote address.
      remoteInstances.forEach(this::syncToRemoteInstance);
    });
  }

  public boolean saveReplicationData(String fileName, File uploadedFile, String leader,
                                     String sender) {
    Path replicationDir = getReplicationDirFor(leader);
    Path saveAsFile = Paths.get(replicationDir.toString(), fileName);
    if (replicationDir.toFile().exists() || replicationDir.toFile().mkdirs()) {
      if (uploadedFile.renameTo(saveAsFile.toFile())) {
        LOG.debug("Store platform backup received from leader {} via {} as {}.",
          leader, sender, saveAsFile);
        return true;
      }
    }
    LOG.error("Could not store platform backup received from leader {} via {} as {}", leader,
      sender, saveAsFile);
    return false;
  }

  private abstract class PlatformBackupParams {
    // The addr that the prometheus server is running on.
    private final String prometheusHost;
    // The username that YW uses to connect to it's DB.
    private final String dbUsername;
    // The password that YW uses to authenticate connections to it's DB.
    private final String dbPassword;
    // The addr that the DB is listening to connection requests on.
    private final String dbHost;
    // The port that the DB is listening to connection requests on.
    private final int dbPort;

    protected PlatformBackupParams() {
      RuntimeConfig<Model> appConfig = runtimeConfigFactory.globalRuntimeConf();
      this.prometheusHost = appConfig.getString(PROMETHEUS_HOST_CONFIG_KEY);
      this.dbUsername = appConfig.getString(DB_USERNAME_CONFIG_KEY);
      this.dbPassword = appConfig.getString(DB_PASSWORD_CONFIG_KEY);
      this.dbHost = appConfig.getString(DB_HOST_CONFIG_KEY);
      this.dbPort = appConfig.getInt(DB_PORT_CONFIG_KEY);
    }

    protected abstract List<String> getCommandSpecificArgs();

    public List<String> getCommandArgs() {
      List<String> commandArgs = new ArrayList<>();
      commandArgs.add(BACKUP_SCRIPT);
      commandArgs.addAll(getCommandSpecificArgs());
      commandArgs.add("--db_username");
      commandArgs.add(dbUsername);
      commandArgs.add("--db_host");
      commandArgs.add(dbHost);
      commandArgs.add("--db_port");
      commandArgs.add(Integer.toString(dbPort));
      commandArgs.add("--prometheus_host");
      commandArgs.add(prometheusHost);
      commandArgs.add("--verbose");
      commandArgs.add("--skip_restart");

      return commandArgs;
    }

    public Map<String, String> getExtraVars() {
      Map<String, String> extraVars = new HashMap<>();

      if (dbPassword != null && !dbPassword.isEmpty()) {
        // Add PGPASSWORD env var to skip having to enter the db password for pg_dump/pg_restore.
        extraVars.put(DB_PASSWORD_ENV_VAR_KEY, dbPassword);
      }

      return extraVars;
    }
  }

  private class CreatePlatformBackupParams extends PlatformBackupParams {
    // Whether to exclude prometheus metric data from the backup or not.
    private final boolean excludePrometheus;
    // Whether to exclude the YB release binaries from the backup or not.
    private final boolean excludeReleases;
    // Where to output the platform backup
    private final String outputDirectory;

    public CreatePlatformBackupParams() {
      this.excludePrometheus = true;
      this.excludeReleases = true;
      this.outputDirectory = getBackupDir().toAbsolutePath().toString();
    }

    @Override
    protected List<String> getCommandSpecificArgs() {
      List<String> commandArgs = new ArrayList<>();
      commandArgs.add("create");

      if (excludePrometheus) {
        commandArgs.add("--exclude_prometheus");
      }

      if (excludeReleases) {
        commandArgs.add("--exclude_releases");
      }

      commandArgs.add("--output");
      commandArgs.add(outputDirectory);

      return commandArgs;
    }
  }

  private class RestorePlatformBackupParams extends PlatformBackupParams {
    // Where to input a previously taken platform backup from.
    private final String input;

    public RestorePlatformBackupParams(String input) {
      this.input = input;
    }

    @Override
    protected List<String> getCommandSpecificArgs() {
      List<String> commandArgs = new ArrayList<>();
      commandArgs.add("restore");
      commandArgs.add("--input");
      commandArgs.add(input);

      return commandArgs;
    }
  }

  private synchronized <T extends PlatformBackupParams> ShellResponse runCommand(T params) {
    List<String> commandArgs = params.getCommandArgs();
    Map<String, String> extraVars = params.getExtraVars();

    LOG.debug("Command to run: [" + String.join(" ", commandArgs) + "]");

    return shellProcessHandler.run(commandArgs, extraVars);
  }

  /**
   * Create a backup of the Yugabyte Platform
   *
   * @return the output/results of running the script
   */
  public boolean createBackup() {
    LOG.info("Creating platform backup...");

    ShellResponse response = runCommand(new CreatePlatformBackupParams());

    if (response.code != 0) {
      LOG.error("Backup failed: " + response.message);
    }

    return response.code == 0;
  }

  /**
   * Restore a backup of the Yugabyte Platform
   *
   * @param input is the path to the backup to be restored
   * @return the output/results of running the script
   */
  public boolean restoreBackup(String input) {
    LOG.info("Restoring platform backup...");

    ShellResponse response = runCommand(new RestorePlatformBackupParams(input));
    if (response.code != 0) {
      LOG.error("Restore failed: " + response.message);
    }
    return response.code == 0;
  }

  private Path getReplicationDirFor(String leader) {
    String storagePath = runtimeConfigFactory.globalRuntimeConf().getString(STORAGE_PATH_KEY);
    return Paths.get(storagePath, REPLICATION_DIR, leader);
  }
}
