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

import akka.actor.ActorSystem;
import akka.actor.Cancellable;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import scala.concurrent.ExecutionContext;


import java.io.*;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.*;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import java.util.stream.StreamSupport;

import static com.yugabyte.yw.common.ha.PlatformReplicationHelper.REPLICATION_FREQUENCY_KEY;

@Singleton
public class PlatformReplicationManager {
  public static final String BACKUP_FILE_PATTERN = "backup_*.tgz";
  private static final String BACKUP_SCRIPT = "bin/yb_platform_backup.sh";
  static final String DB_PASSWORD_ENV_VAR_KEY = "PGPASSWORD";

  private final AtomicReference<Cancellable> schedule;

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final PlatformReplicationHelper replicationHelper;

  private final ShellProcessHandler shellProcessHandler;

  private static final Logger LOG = LoggerFactory.getLogger(PlatformReplicationManager.class);

  @Inject
  public PlatformReplicationManager(
    ActorSystem actorSystem,
    ExecutionContext executionContext,
    ShellProcessHandler shellProcessHandler,
    PlatformReplicationHelper replicationHelper
  ) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.shellProcessHandler = shellProcessHandler;
    this.replicationHelper = replicationHelper;
    this.schedule = new AtomicReference<>(null);
  }

  private Cancellable getSchedule() {
    return this.schedule.get();
  }

  public void start() {
    if (replicationHelper.isBackupScheduleRunning(this.getSchedule())) {
      LOG.warn("Platform backup schedule is already started");
      return;
    }

    if (!replicationHelper.isBackupScheduleEnabled()) {
      LOG.debug("Cannot start backup schedule because it is disabled");
      return;
    }

    Duration frequency = replicationHelper.getBackupFrequency();

    if (!frequency.isNegative() && !frequency.isZero()) {
      this.setSchedule(frequency);
    }
  }

  public void stop() {
    if (!replicationHelper.isBackupScheduleRunning(this.getSchedule())) {
      LOG.warn("Platform backup schedule is already stopped");
      return;
    }

    if (!this.getSchedule().cancel()) {
      LOG.warn("Unknown error occurred stopping platform backup schedule");
    }
  }

  public void init() {
    // Start periodic platform sync schedule if enabled.
    this.start();
    // Switch prometheus to federated if this platform is a follower for HA.
    replicationHelper.ensurePrometheusConfig();
  }

  public JsonNode stopAndDisable() {
    this.stop();
    replicationHelper.setBackupScheduleEnabled(false);

    return this.getBackupInfo();
  }

  public JsonNode setFrequencyStartAndEnable(Duration duration) {
    this.stop();
    replicationHelper.getRuntimeConfig().setValue(
      REPLICATION_FREQUENCY_KEY,
      String.format("%d ms", duration.toMillis())
    );
    replicationHelper.setBackupScheduleEnabled(true);
    this.start();

    return this.getBackupInfo();
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

  // TODO: (Daniel/Shashank) - https://github.com/yugabyte/yugabyte-db/issues/6961.
  public List<File> listBackups(URL leader) throws Exception {
    Path backupDir = replicationHelper.getReplicationDirFor(leader.getHost());

    if (!backupDir.toFile().exists() || !backupDir.toFile().isDirectory()) {
      LOG.debug(String.format("%s directory does not exist", backupDir.toFile().getName()));

      return new ArrayList<>();
    }

    return Util.listFiles(backupDir, BACKUP_FILE_PATTERN);
  }

  public JsonNode getBackupInfo() {
    return replicationHelper.getBackupInfoJson(
      replicationHelper.getBackupFrequency().toMillis(),
      replicationHelper.isBackupScheduleRunning(this.getSchedule())
    );
  }

  public void demoteLocalInstance(
    PlatformInstance localInstance,
    String leaderAddr
  ) throws MalformedURLException {
    if (!localInstance.getIsLocal()) {
      throw new RuntimeException("Cannot perform this action on a remote instance");
    }

    // Stop the old backup schedule.
    this.stopAndDisable();

    // Demote the local instance to follower.
    localInstance.demote();

    // Try switching local prometheus to read from the reported leader.
    replicationHelper.switchPrometheusToFederated(new URL(leaderAddr));
  }

  public void promoteLocalInstance(PlatformInstance newLeader) throws Exception {
    HighAvailabilityConfig config = newLeader.getConfig();
    // Update which instance should be local.
    config.getLocal().setIsLocalAndUpdate(false);
    config.getInstances()
      .forEach(i -> {
        i.setIsLocalAndUpdate(i.getUUID().equals(newLeader.getUUID()));
        try {
          // Clear out any old backups.
          this.cleanupReceivedBackups(new URL(i.getAddress()), 0);
        } catch (MalformedURLException ignored) {}
      });
    // Mark the failover timestamp.
    config.updateLastFailover();
    // Attempt to ensure all remote instances are in follower state.
    // Remotely demote any instance reporting to be a leader.
    config.getRemoteInstances().forEach(replicationHelper::demoteRemoteInstance);
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

  @VisibleForTesting
  boolean sendBackup(PlatformInstance remoteInstance) {
    HighAvailabilityConfig config = remoteInstance.getConfig();
    String clusterKey = config.getClusterKey();
    File recentBackup;
    recentBackup = getMostRecentBackup();
    return recentBackup != null && replicationHelper.exportBackups(
      config, clusterKey, remoteInstance.getAddress(), recentBackup) &&
      remoteInstance.updateLastBackup();
  }

  private File getMostRecentBackup() {
    try {
      return Util.listFiles(replicationHelper.getBackupDir(), BACKUP_FILE_PATTERN).get(0);
    } catch (Exception exception) {
      LOG.error("Could not locate recent backup", exception);
    }
    return null;
  }

  private void syncToRemoteInstance(PlatformInstance remoteInstance) {
    HighAvailabilityConfig config = remoteInstance.getConfig();
    String remoteAddr = remoteInstance.getAddress();
    LOG.debug("Syncing data to " + remoteAddr + "...");

    // Ensure that the remote instance is demoted if this instance is the most current leader.
    if (!replicationHelper.demoteRemoteInstance(remoteInstance)) {
      LOG.error("Error demoting remote instance " + remoteAddr);

      return;
    }

    // Send the platform backup to the remote instance.
    if (!this.sendBackup(remoteInstance)) {
      LOG.error("Error sending platform backup to " + remoteAddr);

      return;
    }

    // Sync the HA cluster metadata to the remote instance.
    replicationHelper.exportPlatformInstances(config, remoteAddr);
  }

  public void oneOffSync() {
    if (replicationHelper.isBackupScheduleEnabled()) {
      this.sync();
    }
  }

  private synchronized void sync() {
    HighAvailabilityConfig.list().forEach(config -> {
      try {
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

        // Remove locally created backups since they have already been sent to followers.
        cleanupCreatedBackups();
      } catch (Exception e) {
        LOG.error("Error running sync for HA config {}", config.getUUID(), e);
      }
    });
  }

  public void cleanupReceivedBackups(URL leader) {
    this.cleanupReceivedBackups(leader, replicationHelper.getNumBackupsRetention());
  }

  private void cleanupReceivedBackups(URL leader, int numToRetain) {
    try {
      List<File> backups = this.listBackups(leader);
      replicationHelper.cleanupBackups(backups, numToRetain);
    } catch (Exception e) {
      LOG.error("Error garbage collecting old backups", e);
    }
  }

  private void cleanupCreatedBackups() {
    try {
      List<File> backups = Util.listFiles(replicationHelper.getBackupDir(), BACKUP_FILE_PATTERN);
      replicationHelper.cleanupBackups(backups, 0);
    } catch (IOException ioException) {
      LOG.warn("Failed to list or delete backups");
    }
  }

  public boolean saveReplicationData(String fileName, File uploadedFile, URL leader,
                                     URL sender) {
    Path replicationDir = replicationHelper.getReplicationDirFor(leader.getHost());
    Path saveAsFile = Paths.get(replicationDir.toString(), fileName);
    if (replicationDir.toFile().exists() || replicationDir.toFile().mkdirs()) {
      if (uploadedFile.renameTo(saveAsFile.toFile())) {
        LOG.debug("Store platform backup received from leader {} via {} as {}.",
          leader.toString(), sender.toString(), saveAsFile);
        return true;
      }
    }
    LOG.error("Could not store platform backup received from leader {} via {} as {}",
      leader.toString(), sender.toString(), saveAsFile);
    return false;
  }

  public void switchPrometheusToStandalone() {
    try {
      this.replicationHelper.switchPrometheusToStandalone();
    } catch (Exception e) {
      LOG.error("Could not switch prometheus config from federated", e);
    }
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
      this.prometheusHost = replicationHelper.getPrometheusHost();
      this.dbUsername = replicationHelper.getDBUser();
      this.dbPassword = replicationHelper.getDBPassword();
      this.dbHost = replicationHelper.getDBHost();
      this.dbPort = replicationHelper.getDBPort();
    }

    protected abstract List<String> getCommandSpecificArgs();

    List<String> getCommandArgs() {
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

    Map<String, String> getExtraVars() {
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

    CreatePlatformBackupParams() {
      this.excludePrometheus = true;
      this.excludeReleases = true;
      this.outputDirectory = replicationHelper.getBackupDir().toString();
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

    RestorePlatformBackupParams(String input) {
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
  @VisibleForTesting
  boolean createBackup() {
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
}
