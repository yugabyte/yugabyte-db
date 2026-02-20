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

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.PrometheusConfigHelper;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.services.FileDataService;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.PlatformInstance.State;
import io.ebean.DB;
import io.ebean.annotation.Transactional;
import io.prometheus.metrics.core.metrics.Gauge;
import io.prometheus.metrics.model.registry.PrometheusRegistry;
import java.io.File;
import java.io.IOException;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.pekko.actor.Cancellable;

@Singleton
@Slf4j
public class PlatformReplicationManager {

  private static final String BACKUP_SCRIPT = "bin/yb_platform_backup.sh";
  static final String DB_PASSWORD_ENV_VAR_KEY = "PGPASSWORD";

  @VisibleForTesting
  public static final String NO_LOCAL_INSTANCE_MSG = "NO LOCAL INSTANCE! Won't sync";

  private static final String INSTANCE_ADDRESS_LABEL = "instance_address";

  private final AtomicReference<Cancellable> schedule;

  private final PlatformScheduler platformScheduler;

  private final PlatformReplicationHelper replicationHelper;

  private final FileDataService fileDataService;

  private final PrometheusConfigHelper prometheusConfigHelper;

  private final ConfigHelper configHelper;

  private final RuntimeConfGetter confGetter;

  public static final Gauge HA_LAST_BACKUP_TIME =
      Gauge.builder()
          .name("yba_ha_last_backup_seconds")
          .help("Last backup time for remote instances")
          .labelNames(INSTANCE_ADDRESS_LABEL)
          .register(PrometheusRegistry.defaultRegistry);

  public static final Gauge HA_LAST_BACKUP_SIZE =
      Gauge.builder()
          .name("yba_ha_last_backup_size_mb")
          .help("Last backup size for remote instances")
          .register(PrometheusRegistry.defaultRegistry);

  @Inject
  public PlatformReplicationManager(
      PlatformScheduler platformScheduler,
      PlatformReplicationHelper replicationHelper,
      FileDataService fileDataService,
      PrometheusConfigHelper prometheusConfigHelper,
      ConfigHelper configHelper,
      RuntimeConfGetter confGetter) {
    this.platformScheduler = platformScheduler;
    this.replicationHelper = replicationHelper;
    this.fileDataService = fileDataService;
    this.prometheusConfigHelper = prometheusConfigHelper;
    this.configHelper = configHelper;
    this.confGetter = confGetter;
    this.schedule = new AtomicReference<>();
  }

  private Cancellable getSchedule() {
    return this.schedule.get();
  }

  public synchronized void start() {
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

  public synchronized void stop() {
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
    this.clearMetrics();

    return this.getBackupInfo();
  }

  public void clearMetrics(PlatformInstance remoteInstance) {
    replicationHelper.clearMetrics(remoteInstance.getConfig(), remoteInstance.getAddress());
    this.clearMetrics();
  }

  public void clearMetrics() {
    HA_LAST_BACKUP_TIME.clear();
    HA_LAST_BACKUP_SIZE.clear();
  }

  public JsonNode setFrequencyStartAndEnable(Duration duration) {
    this.stop();
    replicationHelper.setReplicationFrequency(duration);
    replicationHelper.setBackupScheduleEnabled(true);
    this.start();
    return getBackupInfo();
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

  /** Validates that the request coming from the remote host to change the leader is not stale. */
  public void validateSwitchLeaderRequestForStaleness(
      HighAvailabilityConfig config, String requestLeaderAddr, Date requestLastFailover) {
    Date localLastFailover = config.getLastFailover();
    if (localLastFailover == null) {
      log.debug("No failover has happened because last failover timestamp is not set");
      return;
    }
    log.debug(
        "Local last failover time='{}' and request failover time='{}' from remote host='{}'",
        localLastFailover,
        requestLastFailover,
        requestLeaderAddr);
    int result = localLastFailover.compareTo(requestLastFailover);
    if (result < 0) {
      return;
    }
    if (result == 0) {
      // Ensure the request originates from the same leader if the timestamp is the same.
      PlatformInstance localLeader = config.getLeader().orElse(null);
      if (localLeader == null || localLeader.getAddress().equals(requestLeaderAddr)) {
        return;
      }
    }
    log.error(
        "Rejecting request time={} from {} due to more recent last failover time={}",
        requestLastFailover,
        requestLeaderAddr,
        localLastFailover);
    throw new PlatformServiceException(
        BAD_REQUEST, "Cannot accept request from stale leader " + requestLeaderAddr);
  }

  /** Demote the local instance that is invoked by a remote peer. */
  @Transactional
  public PlatformInstance demoteLocalInstance(
      HighAvailabilityConfig config, String requestLeaderAddr, Date requestLastFailover) {
    PlatformInstance localInstance =
        config
            .getLocal()
            .orElseThrow(
                () -> new PlatformServiceException(BAD_REQUEST, "No local instance configured"));
    log.info(
        "Demoting local instance {} in favor of leader {}",
        localInstance.getAddress(),
        requestLeaderAddr);
    if (!localInstance.isLocal()) {
      throw new RuntimeException("Cannot perform this action on a remote instance");
    }
    validateSwitchLeaderRequestForStaleness(config, requestLeaderAddr, requestLastFailover);

    if (localInstance.getAddress().equals(requestLeaderAddr)) {
      log.warn("Detected partial promotion failure after backup restoration");
      if (!updateLocalInstanceAfterRestore(config)) {
        throw new RuntimeException(
            String.format(
                "Remote address %s is same as the local address %s. It cannot be fixed",
                localInstance.getAddress(), requestLeaderAddr));
      }
      localInstance = config.getLocal().get();
    }

    config.updateLastFailover(requestLastFailover);

    // Stop the old backup schedule.
    stopAndDisable();

    boolean wasLeader = localInstance.isLeader();
    // Demote the local instance to follower.
    localInstance.demote();

    // Set the existing leader to follower to avoid uniqueness violation.
    config.getInstances().stream()
        .sorted(Comparator.comparing(PlatformInstance::isLeader).reversed())
        .forEach(
            i -> {
              boolean isNewLeader = i.getAddress().equals(requestLeaderAddr);
              if (i.isLeader() ^ isNewLeader) {
                // Update only when there is a difference.
                log.debug(
                    "Updating instance {}(uuid={},  isLeader={}) to isLeader={}",
                    i.getAddress(),
                    i.getUuid(),
                    i.isLeader(),
                    isNewLeader);
                i.setState(State.LEADER);
                i.update();
              }
            });
    // Any failure inside the condition rollbacks the DB updates.
    // This conditional check is just for optimization the prometheus mode switch.
    if (wasLeader) {
      // Try switching local prometheus to read from the reported leader.
      replicationHelper.switchPrometheusToFederated(Util.toURL(requestLeaderAddr));
    }
    String version =
        configHelper
            .getConfig(ConfigType.YugawareMetadata)
            .getOrDefault("version", "UNKNOWN")
            .toString();
    localInstance.setYbaVersion(version);
    localInstance.update();
    return localInstance;
  }

  @VisibleForTesting
  public boolean updateLocalInstanceAfterRestore(HighAvailabilityConfig config) {
    AtomicBoolean updated = new AtomicBoolean();
    Optional<HighAvailabilityConfig> localConfig =
        maybeGetLocalHighAvailabilityConfig(config.getClusterKey());
    PlatformInstance localInstance =
        localConfig.isPresent() ? localConfig.get().getLocal().orElse(null) : null;
    if (localInstance != null) {
      config.getInstances().stream()
          .sorted(Comparator.comparing(PlatformInstance::isLocal).reversed())
          .forEach(
              i -> {
                log.debug(
                    "Updating instance {}(uuid={}, isLocal={}, isLeader={})",
                    i.getAddress(),
                    i.getUuid(),
                    i.isLocal(),
                    i.isLeader());
                boolean isLocal = i.getAddress().equals(localInstance.getAddress());
                i.updateLocal(isLocal);
                if (isLocal) {
                  updated.set(isLocal);
                }
              });
    }
    return updated.get();
  }

  public void promoteLocalInstance(PlatformInstance newLeader) {
    log.info("Promoting local instance {} to active.", newLeader.getAddress());
    HighAvailabilityConfig config = newLeader.getConfig();
    config.refresh();
    // Update is_local after the backup is restored.
    if (!config.getLocal().isPresent()) {
      // It must update a local instance.
      throw new RuntimeException("No local instance associated with backup being restored");
    }
    // Promote the new local leader first because the remote demotion response is ignored for
    // eventual consistency. Otherwise, all of them be in standby if local promotion is done later.
    persistLocalInstancePromotion(config, newLeader);
    // Attempt to ensure all remote instances are in follower state.
    // Remotely demote any instance reporting to be a leader.
    config
        .getRemoteInstances()
        .forEach(
            instance -> {
              log.info(
                  "Demoting remote instance {} in favor of {}",
                  instance.getAddress(),
                  newLeader.getAddress());
              // As the error is not propagated, there can be split brain due to communication
              // failure that will be fixed ultimately when the communication is restored due to
              // background sync.
              if (!replicationHelper.demoteRemoteInstance(instance, true)) {
                log.warn("Could not demote remote instance {}", instance.getAddress());
              }
              try {
                // Clear out any old backups only after the leader promotion is successful.
                log.info("Cleaning up received backups from {}", instance.getAddress());
                replicationHelper.cleanupReceivedBackups(Util.toURL(instance.getAddress()), 0);
              } catch (Exception ignored) {
                log.warn(
                    "Error cleaning up received backups from {} - {}",
                    instance.getAddress(),
                    ignored.getMessage());
              }
            });
  }

  @Transactional
  private void persistLocalInstancePromotion(
      HighAvailabilityConfig config, PlatformInstance localInstance) {
    // Mark the failover timestamp.
    config.updateLastFailover();
    // Only one leader can be at a time. Demote the remote record first.
    config.getRemoteInstances().forEach(PlatformInstance::demote);
    localInstance.refresh();
    localInstance.promote();
    // Start the new backup schedule.
    start();
    // Finally, switch the prometheus configuration to read from swamper targets directly.
    switchPrometheusToStandalone();
    oneOffSync();
  }

  /**
   * A method to import a list of platform instances received from the leader platform instance.
   * Assumption is that any platform instance existing locally but not provided in the payload has
   * been deleted on the leader, and thus should be deleted here too.
   *
   * @param config the local HA Config model
   * @param newInstances the JSON payload received from the leader instance
   */
  @Transactional
  public Set<PlatformInstance> importPlatformInstances(
      HighAvailabilityConfig config,
      List<PlatformInstance> newInstances,
      Date requestLastFailover) {
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

    // Get the leader instance. It must be present as the leader sends the request.
    PlatformInstance leaderInstance =
        newInstances.stream()
            .filter(PlatformInstance::isLeader)
            .findFirst()
            .orElseThrow(
                () ->
                    new PlatformServiceException(
                        BAD_REQUEST, "Leader must be included by the sender"));

    validateSwitchLeaderRequestForStaleness(
        config, leaderInstance.getAddress(), requestLastFailover);

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
        .map(this::processImportedInstance)
        .filter(Optional::isPresent)
        .map(Optional::get)
        .collect(Collectors.toSet());
  }

  @Transactional
  private Optional<PlatformInstance> processImportedInstance(PlatformInstance i) {
    Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get();
    if (config.isPresent()) {
      // Ensure the previous leader is marked as a follower to avoid uniqueness violation.
      if (i.isLeader()) {
        Optional<PlatformInstance> existingLeader = config.get().getLeader();
        if (existingLeader.isPresent()
            && !existingLeader.get().getAddress().equals(i.getAddress())) {
          existingLeader.get().demote();
        }
      }
      Optional<PlatformInstance> existingInstance = PlatformInstance.getByAddress(i.getAddress());
      if (existingInstance.isPresent()) {
        // Since we sync instances after sending backups, the leader instance has the source of
        // truth as to when the last backup has been successfully sent to followers.
        existingInstance.get().setLastBackup(i.getLastBackup());
        existingInstance.get().setState(i.isLeader() ? State.LEADER : State.STAND_BY);
        existingInstance.get().update();
        i = existingInstance.get();
      } else {
        i.setLocal(false);
        i.setConfig(config.get());
        i.save();
      }
      return Optional.of(i);
    }
    return Optional.empty();
  }

  public boolean testConnection(
      HighAvailabilityConfig config, String address, boolean acceptAnyCertificate) {
    boolean result =
        replicationHelper.testConnection(
            config, config.getClusterKey(), address, acceptAnyCertificate);
    if (!result) {
      log.error("Error testing connection to {}", address);
    }
    return result;
  }

  @VisibleForTesting
  public boolean sendBackup(PlatformInstance remoteInstance) {
    HighAvailabilityConfig config = remoteInstance.getConfig();
    String clusterKey = config.getClusterKey();
    boolean result =
        replicationHelper
            .getMostRecentBackup()
            .map(
                backup -> {
                  HA_LAST_BACKUP_SIZE.set(backup.length() / (1024.0 * 1024.0));
                  return replicationHelper.exportBackups(
                      config, clusterKey, remoteInstance.getAddress(), backup);
                })
            .orElse(false);
    if (!result) {
      log.error("Error sending platform backup to {}", remoteInstance.getAddress());
      // Clear version mismatch metric
      replicationHelper.clearMetrics(config, remoteInstance.getAddress());
    }

    return result;
  }

  public void oneOffSync() {
    if (replicationHelper.isBackupScheduleEnabled()) {
      this.sync();
    }
  }

  private boolean precheckSyncCondition(HighAvailabilityConfig config) {
    Optional<PlatformInstance> localInstance = config.getLocal();
    if (!localInstance.isPresent()) {
      log.error(NO_LOCAL_INSTANCE_MSG);
      return false;
    }
    // No point in taking a backup if there is no one to send it to.
    if (config.getRemoteInstances().isEmpty()) {
      log.debug("Skipping HA cluster sync...");
      return false;
    }
    Optional<PlatformInstance> leader = config.getLeader();
    if (!leader.isPresent()) {
      log.warn("No leader is found");
      return false;
    }
    if (!leader.get().getUuid().equals(localInstance.get().getUuid())) {
      log.debug("Skipping sync because the local instance is not the leader");
      return false;
    }
    return true;
  }

  private void sync() {
    try {
      // No need to lock as taking a backup is ok. The sync will get rejected by the remote if the
      // switch happens in the middle.
      HighAvailabilityConfig.get()
          .ifPresent(
              config -> {
                try {
                  if (!precheckSyncCondition(config)) {
                    return;
                  }
                  Optional<PlatformInstance> localInstance = config.getLocal();
                  List<PlatformInstance> remoteInstances = config.getRemoteInstances();
                  AtomicBoolean backupCreated = new AtomicBoolean();
                  try {
                    // Create the platform backup.
                    if (createBackup()) {
                      backupCreated.set(true);
                      // Update local last backup time since creating the backup succeeded.
                      localInstance.get().updateLastBackup();
                    } else {
                      log.error("Error creating platform backup");
                    }
                  } catch (Exception e) {
                    log.error("Error creating platform backup", e);
                  }
                  remoteInstances.forEach(
                      instance -> {
                        try {
                          // Sync first before taking the backup to propagate the config faster.
                          // TODO Put this on a different schedule to sync faster?
                          if (replicationHelper.syncToRemoteInstance(instance)) {
                            if (backupCreated.get()) {
                              try {
                                Date lastLastBackup = instance.getLastBackup();
                                instance.updateLastBackup(localInstance.get().getLastBackup());
                                if (!sendBackup(instance)) {
                                  instance.updateLastBackup(lastLastBackup);
                                }
                              } catch (Exception e) {
                                log.error(
                                    "Exception {} sending backup to instance {}",
                                    e.getMessage(),
                                    instance.getAddress());
                              }
                            }
                          } else {
                            replicationHelper.clearMetrics(config, instance.getAddress());
                            log.error(
                                "Error syncing config to remote instance {}",
                                instance.getAddress());
                          }
                        } catch (Exception e) {
                          log.error(
                              "Exception {} syncing config to remote instance {}",
                              e.getMessage(),
                              instance.getAddress());
                        }
                      });
                  remoteInstances.forEach(
                      instance -> {
                        try {
                          replicationHelper.syncToRemoteInstance(instance);
                        } catch (Exception e) {
                          log.warn("Error in final sync to instance {}", instance.getAddress(), e);
                        }
                      });
                  // Export metric on last backup.
                  remoteInstances.stream()
                      .forEach(
                          instance -> {
                            if (instance.getLastBackup() != null) {
                              HA_LAST_BACKUP_TIME
                                  .labelValues(instance.getAddress())
                                  .set(instance.getLastBackup().toInstant().getEpochSecond());
                            }
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
            leader,
            sender,
            saveAsFile);

        return true;
      } catch (IOException ioException) {
        log.error("File move failed from {} as {}", uploadedFile, saveAsFile, ioException);
      }
    } else {
      log.error(
          "Couldn't create folder {} to store platform backup received from leader {} via {} to {}",
          replicationDir,
          leader,
          sender,
          saveAsFile);
    }

    return false;
  }

  public void switchPrometheusToStandalone() {
    replicationHelper.switchPrometheusToStandalone();
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

  public class CreatePlatformBackupParams extends PlatformBackupParams {

    // Whether to exclude prometheus metric data from the backup or not.
    private final boolean excludePrometheus;
    // Whether to exclude the YB release binaries from the backup or not.
    private final boolean excludeReleases;
    // Where to output the platform backup
    private final String outputDirectory;

    public CreatePlatformBackupParams() {
      this(true, true, replicationHelper.getBackupDir().toString());
    }

    public CreatePlatformBackupParams(
        boolean excludePrometheus, boolean excludeReleases, String outputDirectory) {
      this.excludePrometheus = excludePrometheus;
      this.excludeReleases = excludeReleases;
      this.outputDirectory = outputDirectory;
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

  public class RestorePlatformBackupParams extends PlatformBackupParams {

    // Where to input a previously taken platform backup from.
    private final File input;
    private final boolean k8sRestoreYbaDbOnRestart;
    private final boolean skipOldFiles;

    public RestorePlatformBackupParams(
        File input, boolean k8sRestoreYbaDbOnRestart, boolean skipOldFiles) {
      this.input = input;
      this.k8sRestoreYbaDbOnRestart = k8sRestoreYbaDbOnRestart;
      this.skipOldFiles = skipOldFiles;
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
      } else if (k8sRestoreYbaDbOnRestart
          && confGetter.getGlobalConf(GlobalConfKeys.k8sYbaRestoreSkipDumpFileDelete)) {
        commandArgs.add("--skip_dump_file_delete");
      }
      if (skipOldFiles) {
        commandArgs.add("--skip_old_files");
      }
      if (!confGetter.getGlobalConf(GlobalConfKeys.disablePlatformHARestoreTransaction)) {
        log.debug("Setting --single-transaction for platform HA restore");
        commandArgs.add("--single_transaction");
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
      log.error("Backup failed: {}", response.message);
    }

    return response.code == 0;
  }

  public boolean restoreBackupOnStandby(HighAvailabilityConfig config, File input) {
    boolean succeeded =
        restoreBackup(input, false /* k8sRestoreYbaDbOnRestart */, false /*skipOldFiles*/);
    if (succeeded) {
      config.refresh();
      // Fix the local instance after restore.
      updateLocalInstanceAfterRestore(config);
      // Keep the local instance as follower after restore.
      config
          .getLocal()
          .orElseThrow(
              () ->
                  new PlatformServiceException(
                      BAD_REQUEST, "Local instance not found after restore"))
          .demote();
    }
    return succeeded;
  }

  public boolean restoreBackup(File input, boolean k8sRestoreYbaDbOnRestart) {
    return restoreBackup(input, k8sRestoreYbaDbOnRestart, false);
  }

  /**
   * Restore a backup of the YugabyteDB Anywhere
   *
   * @param input is the path to the backup to be restored
   * @param k8sRestoreYbaDbOnRestart restore Yba DB on restart in k8s
   * @param skipOldFiles skip restoring old files
   * @return the output/results of running the script
   */
  public boolean restoreBackup(File input, boolean k8sRestoreYbaDbOnRestart, boolean skipOldFiles) {
    log.info("Restoring platform backup...");
    ShellResponse response =
        replicationHelper.runCommand(
            new RestorePlatformBackupParams(input, k8sRestoreYbaDbOnRestart, skipOldFiles));
    if (response.code != 0) {
      log.error("Restore failed: {}", response.message);
    } else {
      log.info("Platform backup restored successfully");
      DB.cacheManager().clearAll();
      // Wait for DB connection to be available after restore.
      // Restore wipes out tables, invalidating the underlying connections.
      Util.waitForDBConnection(5);
      // Sync the files stored in DB to FS in case restore is successful.
      fileDataService.syncFileData(AppConfigHelper.getStoragePath(), true);
    }
    return response.code == 0;
  }

  /**
   * Save the HA config to a local JSON file generally before a restore of the DB.
   *
   * @param config the current local HA config.
   * @return the path to the file.
   */
  public synchronized Path saveLocalHighAvailabilityConfig(HighAvailabilityConfig config) {
    return replicationHelper.saveLocalHighAvailabilityConfig(config);
  }

  /**
   * Reads the HA config from the local JSON file that was saved earlier.
   *
   * @param clusterKey the HA config cluster key.
   * @return the HA config.
   */
  public synchronized Optional<HighAvailabilityConfig> maybeGetLocalHighAvailabilityConfig(
      String clusterKey) {
    return replicationHelper.maybeGetLocalHighAvailabilityConfig(clusterKey);
  }

  /**
   * Deletes the local HA config file that was saved earlier.
   *
   * @return true if deleted, else false.
   */
  public synchronized boolean deleteLocalHighAvailabilityConfig() {
    return replicationHelper.deleteLocalHighAvailabilityConfig();
  }
}
