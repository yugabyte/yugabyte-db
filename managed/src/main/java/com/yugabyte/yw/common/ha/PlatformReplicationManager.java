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

import static play.mvc.Http.Status.BAD_REQUEST;

import akka.actor.Cancellable;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.PrometheusConfigHelper;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.services.FileDataService;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import java.io.File;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class PlatformReplicationManager {

  private static final String BACKUP_SCRIPT = "bin/yb_platform_backup.sh";
  static final String DB_PASSWORD_ENV_VAR_KEY = "PGPASSWORD";

  @VisibleForTesting
  public static final String NO_LOCAL_INSTANCE_MSG = "NO LOCAL INSTANCE! Won't sync";

  private final AtomicReference<Cancellable> schedule;

  private final PlatformScheduler platformScheduler;

  private final PlatformReplicationHelper replicationHelper;

  private final FileDataService fileDataService;

  private final PrometheusConfigHelper prometheusConfigHelper;

  @Inject
  public PlatformReplicationManager(
      PlatformScheduler platformScheduler,
      PlatformReplicationHelper replicationHelper,
      FileDataService fileDataService,
      PrometheusConfigHelper prometheusConfigHelper) {
    this.platformScheduler = platformScheduler;
    this.replicationHelper = replicationHelper;
    this.fileDataService = fileDataService;
    this.prometheusConfigHelper = prometheusConfigHelper;
    this.schedule = new AtomicReference<>(null);
  }

  private Cancellable getSchedule() {
    return this.schedule.get();
  }

  public void start() {
    if (replicationHelper.isBackupScheduleRunning(this.getSchedule())) {
      log.warn("Platform backup schedule is already started");
      return;
    }

    if (!replicationHelper.isBackupScheduleEnabled()) {
      log.debug("Cannot start backup schedule because it is disabled");
      return;
    }

    Duration frequency = replicationHelper.getBackupFrequency();

    if (!frequency.isNegative() && !frequency.isZero()) {
      this.schedule.set(this.createSchedule(frequency));
    }
  }

  public void stop() {
    if (!replicationHelper.isBackupScheduleRunning(this.getSchedule())) {
      log.debug("Platform backup schedule is already stopped");
      return;
    }

    if (!this.getSchedule().cancel()) {
      log.warn("Unknown error occurred stopping platform backup schedule");
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
    replicationHelper.setReplicationFrequency(duration);
    replicationHelper.setBackupScheduleEnabled(true);
    this.start();
    return this.getBackupInfo();
  }

  private Cancellable createSchedule(Duration frequency) {
    log.info("Scheduling periodic platform backups every {}", frequency.toString());
    return platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO, // initialDelay
        frequency, // interval
        this::sync);
  }

  public List<File> listBackups(URL leader) {
    return replicationHelper.listBackups(leader);
  }

  public JsonNode getBackupInfo() {
    return replicationHelper.getBackupInfoJson(
        replicationHelper.getBackupFrequency().toMillis(),
        replicationHelper.isBackupScheduleRunning(this.getSchedule()));
  }

  public void demoteLocalInstance(PlatformInstance localInstance, String leaderAddr)
      throws MalformedURLException {
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

  public void promoteLocalInstance(PlatformInstance newLeader) {
    HighAvailabilityConfig config = newLeader.getConfig();
    Optional<PlatformInstance> previousLocal = config.getLocal();

    if (!previousLocal.isPresent()) {
      throw new RuntimeException("No local instance associated with backup being restored");
    }

    // Update which instance should be local.
    previousLocal.get().updateIsLocal(false);
    config
        .getInstances()
        .forEach(
            i -> {
              i.updateIsLocal(i.getUuid().equals(newLeader.getUuid()));
              try {
                // Clear out any old backups.
                replicationHelper.cleanupReceivedBackups(new URL(i.getAddress()), 0);
              } catch (MalformedURLException ignored) {
              }
            });

    // Mark the failover timestamp.
    config.updateLastFailover();
    // Attempt to ensure all remote instances are in follower state.
    // Remotely demote any instance reporting to be a leader.
    config.getRemoteInstances().forEach(replicationHelper::demoteRemoteInstance);
    // Promote the new local leader.
    // we need to refresh because i.setIsLocalAndUpdate updated the underlying db bypassing
    // newLeader bean.
    newLeader.refresh();
    newLeader.promote();
  }

  /**
   * A method to import a list of platform instances received from the leader platform instance.
   * Assumption is that any platform instance existing locally but not provided in the payload has
   * been deleted on the leader, and thus should be deleted here too.
   *
   * @param config the local HA Config model
   * @param newInstances the JSON payload received from the leader instance
   */
  public Set<PlatformInstance> importPlatformInstances(
      HighAvailabilityConfig config, List<PlatformInstance> newInstances) {
    String localAddress = config.getLocal().get().getAddress();

    // Get list of request payload addresses.
    Set<String> newAddrs =
        newInstances.stream().map(PlatformInstance::getAddress).collect(Collectors.toSet());

    if (!newAddrs.contains(localAddress)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Current instance (%s) not found in Sync request %s", localAddress, newAddrs));
    }

    List<PlatformInstance> existingInstances = config.getInstances();
    // Get list of existing addresses.
    Set<String> existingAddrs =
        existingInstances.stream().map(PlatformInstance::getAddress).collect(Collectors.toSet());

    // Delete any instances that exist locally but aren't included in the sync request.
    Set<String> instanceAddrsToDelete = Sets.difference(existingAddrs, newAddrs);
    existingInstances.stream()
        .filter(i -> instanceAddrsToDelete.contains(i.getAddress()))
        .forEach(PlatformInstance::delete);

    // Import the new instances, or update existing ones.
    return newInstances.stream()
        .map(replicationHelper::processImportedInstance)
        .filter(Optional::isPresent)
        .map(Optional::get)
        .collect(Collectors.toSet());
  }

  @VisibleForTesting
  boolean sendBackup(PlatformInstance remoteInstance) {
    HighAvailabilityConfig config = remoteInstance.getConfig();
    String clusterKey = config.getClusterKey();
    boolean result =
        replicationHelper
            .getMostRecentBackup()
            .map(
                backup ->
                    replicationHelper.exportBackups(
                            config, clusterKey, remoteInstance.getAddress(), backup)
                        && remoteInstance.updateLastBackup())
            .orElse(false);
    if (!result) {
      log.error("Error sending platform backup to " + remoteInstance.getAddress());
    }

    return result;
  }

  public void oneOffSync() {
    if (replicationHelper.isBackupScheduleEnabled()) {
      this.sync();
    }
  }

  private synchronized void sync() {
    try {
      HighAvailabilityConfig.get()
          .ifPresent(
              config -> {
                try {
                  if (!config.getLocal().isPresent()) {
                    log.error(NO_LOCAL_INSTANCE_MSG);
                    return;
                  }

                  List<PlatformInstance> remoteInstances = config.getRemoteInstances();
                  // No point in taking a backup if there is no one to send it to.
                  if (remoteInstances.isEmpty()) {
                    log.debug("Skipping HA cluster sync...");

                    return;
                  }

                  // Create the platform backup.
                  if (!this.createBackup()) {
                    log.error("Error creating platform backup");

                    return;
                  }

                  // Update local last backup time if creating the backup succeeded.
                  config
                      .getLocal()
                      .ifPresent(
                          localInstance -> {
                            localInstance.updateLastBackup();

                            // Send the platform backup to all followers.
                            Set<PlatformInstance> instancesToSync =
                                remoteInstances.stream()
                                    .filter(this::sendBackup)
                                    .collect(Collectors.toSet());

                            // Sync the HA cluster state to all followers that successfully received
                            // a
                            // backup.
                            instancesToSync.forEach(replicationHelper::syncToRemoteInstance);
                          });
                } catch (Exception e) {
                  log.error("Error running sync for HA config {}", config.getUuid(), e);
                } finally {
                  // Remove locally created backups since they have already been sent to followers.
                  replicationHelper.cleanupCreatedBackups();
                }
              });
    } catch (Exception e) {
      log.error("Error running platform replication sync", e);
    }
  }

  public void cleanupReceivedBackups(URL leader) {
    replicationHelper.cleanupReceivedBackups(leader, replicationHelper.getNumBackupsRetention());
  }

  public boolean saveReplicationData(String fileName, Path uploadedFile, URL leader, URL sender) {
    Path replicationDir = replicationHelper.getReplicationDirFor(leader.getHost());
    Path saveAsFile = Paths.get(replicationDir.toString(), fileName).normalize();
    if ((replicationDir.toFile().exists() || replicationDir.toFile().mkdirs())
        && saveAsFile.toString().startsWith(replicationDir.toString())) {
      try {
        FileUtils.moveFile(uploadedFile, saveAsFile);
        log.debug(
            "Store platform backup received from leader {} via {} as {}.",
            leader.toString(),
            sender.toString(),
            saveAsFile);

        return true;
      } catch (IOException ioException) {
        log.error("File move failed from {} as {}", uploadedFile, saveAsFile, ioException);
      }
    } else {
      log.error(
          "Couldn't create folder {} to store platform backup received from leader {} via {} to {}",
          replicationDir,
          leader.toString(),
          sender.toString(),
          saveAsFile.toString());
    }

    return false;
  }

  public void switchPrometheusToStandalone() {
    this.replicationHelper.switchPrometheusToStandalone();
  }

  abstract class PlatformBackupParams {

    // The addr that the prometheus server is running on.
    private final String prometheusHost;

    // The port that the prometheus server is running on.
    private final int prometheusPort;
    // The username that YW uses to connect to its DB.
    private final String dbUsername;
    // The password that YW uses to authenticate connections to its DB.
    private final String dbPassword;
    // The addr that the DB is listening to connection requests on.
    private final String dbHost;
    // The port that the DB is listening to connection requests on.
    private final int dbPort;

    protected PlatformBackupParams() {
      this.prometheusHost = prometheusConfigHelper.getPrometheusHost();
      this.prometheusPort = prometheusConfigHelper.getPrometheusPort();
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
      commandArgs.add("--prometheus_port");
      commandArgs.add(String.valueOf(prometheusPort));
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

    List<String> getYbaInstallerArgs() {
      List<String> commandArgs = new ArrayList<>();
      commandArgs.add("--yba_installer");
      commandArgs.add("--data_dir");
      commandArgs.add(replicationHelper.getBaseInstall());

      return commandArgs;
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
      commandArgs.add("--disable_version_check");

      String installation = replicationHelper.getInstallationType();
      if (StringUtils.isNotBlank(installation) && installation.trim().equals("yba-installer")) {
        commandArgs.add("--pg_dump_path");
        commandArgs.add(replicationHelper.getPGDumpPath());
        commandArgs.addAll(getYbaInstallerArgs());
      }

      commandArgs.add("--output");
      commandArgs.add(outputDirectory);

      return commandArgs;
    }
  }

  private class RestorePlatformBackupParams extends PlatformBackupParams {

    // Where to input a previously taken platform backup from.
    private final File input;

    RestorePlatformBackupParams(File input) {
      this.input = input;
    }

    @Override
    protected List<String> getCommandSpecificArgs() {
      List<String> commandArgs = new ArrayList<>();
      commandArgs.add("restore");
      commandArgs.add("--input");
      commandArgs.add(input.getAbsolutePath());
      commandArgs.add("--disable_version_check");
      String installation = replicationHelper.getInstallationType();
      if (StringUtils.isNotBlank(installation) && installation.trim().equals("yba-installer")) {
        commandArgs.add("--pg_restore_path");
        commandArgs.add(replicationHelper.getPGRestorePath());
        commandArgs.addAll(getYbaInstallerArgs());
        commandArgs.add("--destination");
        commandArgs.add(replicationHelper.getBaseInstall());
      }

      return commandArgs;
    }
  }

  /**
   * Create a backup of the YugabyteDB Anywhere
   *
   * @return the output/results of running the script
   */
  @VisibleForTesting
  boolean createBackup() {
    log.debug("Creating platform backup...");

    ShellResponse response = replicationHelper.runCommand(new CreatePlatformBackupParams());

    if (response.code != 0) {
      log.error("Backup failed: " + response.message);
    }

    return response.code == 0;
  }

  /**
   * Restore a backup of the YugabyteDB Anywhere
   *
   * @param input is the path to the backup to be restored
   * @return the output/results of running the script
   */
  public boolean restoreBackup(File input) {
    log.info("Restoring platform backup...");

    ShellResponse response = replicationHelper.runCommand(new RestorePlatformBackupParams(input));
    if (response.code != 0) {
      log.error("Restore failed: " + response.message);
    } else {
      // Sync the files stored in DB to FS in case restore is successful.
      fileDataService.syncFileData(AppConfigHelper.getStoragePath(), true);
    }

    return response.code == 0;
  }
}
