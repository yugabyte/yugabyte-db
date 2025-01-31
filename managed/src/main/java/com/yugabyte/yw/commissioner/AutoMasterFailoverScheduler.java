// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.AutoMasterFailover.Action;
import com.yugabyte.yw.commissioner.AutoMasterFailover.ActionType;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.JobSchedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.schedule.JobConfig;
import com.yugabyte.yw.models.helpers.schedule.JobConfig.RuntimeParams;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig;
import com.yugabyte.yw.scheduler.JobScheduler;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.HashSet;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import lombok.Builder;
import lombok.Getter;
import lombok.extern.jackson.Jacksonized;
import lombok.extern.slf4j.Slf4j;

/**
 * This monitors all the universes to create the schedules for detecting master failure and
 * subsequently schedule the failover task.
 */
@Singleton
@Slf4j
public class AutoMasterFailoverScheduler {
  private static final String AUTO_MASTER_FAILOVER_POOL_NAME = "auto_master_failover.executor";
  private static final String AUTO_MASTER_FAILOVER_SCHEDULE_NAME_FORMAT = "AutoMasterFailover_%s";
  private static final String DETECT_MASTER_FAILURE_SCHEDULE_NAME_FORMAT = "DetectMasterFailure_%s";
  private static final String SUPPORTED_DB_STABLE_VERSION = "2.20.3.0-b10";
  private static final String SUPPORTED_DB_PREVIEW_VERSION = "2.21.0.0-b309";

  private final RuntimeConfGetter confGetter;
  private final PlatformExecutorFactory platformExecutorFactory;
  private final PlatformScheduler platformScheduler;
  private final JobScheduler jobScheduler;
  private final AutoMasterFailover autoMasterFailover;

  private ExecutorService failoverExecutor;

  @Inject
  public AutoMasterFailoverScheduler(
      RuntimeConfGetter confGetter,
      PlatformExecutorFactory platformExecutorFactory,
      PlatformScheduler platformScheduler,
      JobScheduler jobScheduler,
      AutoMasterFailover autoMasterFailover) {
    this.confGetter = confGetter;
    this.platformExecutorFactory = platformExecutorFactory;
    this.platformScheduler = platformScheduler;
    this.jobScheduler = jobScheduler;
    this.autoMasterFailover = autoMasterFailover;
  }

  /** Type of failover scheduled job to be created. */
  public enum FailoverJobType {
    DETECT_MASTER_FAILURE,
    MASTER_FAILOVER,
    SYNC_MASTER_ADDRS
  }

  /** Implementation of the schedule job config for failover. */
  @SuppressWarnings("serial")
  @Getter
  @Builder(toBuilder = true)
  @Jacksonized
  public static class FailoverJobConfig implements JobConfig {
    private UUID customerUuid;
    private UUID universeUuid;
    private FailoverJobType failoverJobType;

    @Override
    public CompletableFuture<?> executeJob(RuntimeParams runtime) {
      Customer customer = Customer.getOrBadRequest(customerUuid);
      AutoMasterFailoverScheduler scheduler =
          runtime.getInjector().getInstance(AutoMasterFailoverScheduler.class);
      AutoMasterFailover automatedMasterFailover =
          runtime.getInjector().getInstance(AutoMasterFailover.class);
      if (!runPrecheck(runtime, customer)) {
        // Return null future to signal skip if precheck fails.
        return null;
      }
      return CompletableFuture.runAsync(
          () -> {
            Optional<Universe> universeOptional = Universe.maybeGet(universeUuid);
            if (!universeOptional.isPresent()) {
              // Delete the schedule promptly.
              runtime.getJobScheduler().deleteSchedule(runtime.getJobSchedule().getUuid());
              return;
            }
            Universe universe = universeOptional.get();
            switch (failoverJobType) {
              case DETECT_MASTER_FAILURE:
                log.debug(
                    "Running master failure detection schedule for universe {}",
                    universe.getUniverseUUID());
                scheduler.detectMasterFailure(customer, universe, runtime);
                break;
              case MASTER_FAILOVER:
              case SYNC_MASTER_ADDRS:
                try {
                  log.debug(
                      "Running auto master failover for universe {}", universe.getUniverseUUID());
                  String detectScheduleName =
                      scheduler.getDetectMasterFailureScheduleName(universe);
                  Optional<JobSchedule> optional =
                      runtime
                          .getJobScheduler()
                          .maybeGetSchedule(customer.getUuid(), detectScheduleName);
                  if (!optional.isPresent()) {
                    log.warn(
                        "Master failover detection schedule is already deleted for universe {}",
                        universe.getUniverseUUID());
                    return;
                  }
                  if (optional.get().getScheduleConfig().isDisabled()) {
                    log.info(
                        "Skipping master failover for universe {} as detection is disabled",
                        universe.getUniverseUUID());
                    return;
                  }

                  automatedMasterFailover
                      .maybeSubmitFailoverMasterTask(customer, universe, runtime)
                      .ifPresent(
                          tf -> {
                            if (tf.getTaskState() != TaskInfo.State.Success) {
                              // Fail the job and keep the schedule to keep track of the failed
                              // counts.
                              String errMsg =
                                  String.format(
                                      "Auto master failover task %s (%s) failed for universe %s",
                                      tf.getTaskType(), tf.getUuid(), universe.getUniverseUUID());
                              throw new RuntimeException(errMsg);
                            }
                          });
                } finally {
                  // Task has completed. Disable the schedule until it is enabled by the detector.
                  runtime
                      .getJobScheduler()
                      .disableSchedule(runtime.getJobSchedule().getUuid(), true);
                }
                break;
              default:
                throw new RuntimeException("Unknown failover job type " + failoverJobType);
            }
          },
          scheduler.failoverExecutor);
    }

    private boolean runPrecheck(RuntimeParams runtime, Customer customer) {
      Optional<Universe> optional = Universe.maybeGet(universeUuid);
      if (!optional.isPresent()) {
        // Delete the schedule promptly.
        runtime.getJobScheduler().deleteSchedule(runtime.getJobSchedule().getUuid());
        return false;
      }
      if (failoverJobType != FailoverJobType.DETECT_MASTER_FAILURE) {
        // Check if the actual fail-over job is still valid.
        Action action =
            runtime
                .getInjector()
                .getInstance(AutoMasterFailover.class)
                .getAllowedMasterFailoverAction(customer, optional.get(), true /*submissionCheck*/);
        return action.getActionType() != ActionType.NONE;
      }
      return true;
    }
  }

  public void init() {
    Duration pollingInterval =
        confGetter.getGlobalConf(GlobalConfKeys.autoMasterFailoverPollerInterval);
    platformScheduler.schedule(
        getClass().getSimpleName(), pollingInterval, pollingInterval, this::createSchedules);
    failoverExecutor =
        platformExecutorFactory.createExecutor(
            AUTO_MASTER_FAILOVER_POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("auto-master-failover-executor-%d").build());
  }

  private String getDetectMasterFailureScheduleName(Universe universe) {
    return String.format(DETECT_MASTER_FAILURE_SCHEDULE_NAME_FORMAT, universe.getUniverseUUID());
  }

  private String getAutoMasterFailoverScheduleName(Universe universe) {
    return String.format(AUTO_MASTER_FAILOVER_SCHEDULE_NAME_FORMAT, universe.getUniverseUUID());
  }

  private void createSchedules() {
    Set<UUID> universeUuids = new HashSet<>();
    Customer.getAll()
        .forEach(
            c ->
                c.getUniverses()
                    .forEach(
                        u -> {
                          UserIntent userIntent =
                              u.getUniverseDetails().getPrimaryCluster().userIntent;
                          if (userIntent == null
                              || userIntent.providerType == CloudType.kubernetes) {
                            return;
                          }
                          boolean isFailoverEnabled =
                              confGetter.getConfForScope(
                                  u, UniverseConfKeys.enableAutoMasterFailover);
                          if (!isFailoverEnabled) {
                            log.debug(
                                "Automated master failover for universe {} is disabled",
                                u.getUniverseUUID());
                            return;
                          }
                          String ybDbVersion = userIntent.ybSoftwareVersion;
                          if (Util.compareYBVersions(
                                  ybDbVersion,
                                  SUPPORTED_DB_STABLE_VERSION,
                                  SUPPORTED_DB_PREVIEW_VERSION,
                                  true)
                              < 0) {
                            log.info(
                                "Auto master failover not supported in current version {}",
                                ybDbVersion);
                            log.info(
                                "Supported versions are from {} (stable) and {} (preview)",
                                SUPPORTED_DB_STABLE_VERSION,
                                SUPPORTED_DB_PREVIEW_VERSION);
                            return;
                          }
                          try {
                            // Create both schedules up front.
                            createMasterFailoverDetectionSchedule(c, u);
                            createMasterFailoverSchedule(c, u);
                          } catch (Exception e) {
                            log.error(
                                "Error in creating master failover schedules for universe {} - {}",
                                u.getUniverseUUID(),
                                e.getMessage());
                          } finally {
                            universeUuids.add(u.getUniverseUUID());
                          }
                        }));
    try {
      // Delete dangling schedules.
      jobScheduler.deleteSchedulesIf(
          FailoverJobConfig.class,
          s -> {
            FailoverJobConfig jobConfig = s.getJobConfig();
            return !universeUuids.contains(jobConfig.getUniverseUuid());
          });
    } catch (Exception e) {
      log.error("Error in deleting master failover schedules {}", e.getMessage());
    }
  }

  private void createMasterFailoverDetectionSchedule(Customer customer, Universe universe) {
    Duration detectionInterval =
        confGetter.getConfForScope(universe, UniverseConfKeys.autoMasterFailoverDetectionInterval);
    log.trace(
        "Master failover detection interval is set to {} seconds for universe {}",
        detectionInterval.getSeconds(),
        universe.getUniverseUUID());
    String detectScheduleName = getDetectMasterFailureScheduleName(universe);
    boolean shouldDisable = universe.getUniverseDetails().universePaused;
    Optional<JobSchedule> optional =
        jobScheduler.maybeGetSchedule(customer.getUuid(), detectScheduleName);
    if (optional.isPresent()) {
      ScheduleConfig scheduleConfig = optional.get().getScheduleConfig();
      if (shouldDisable ^ scheduleConfig.isDisabled()) {
        log.info(
            "Master failover detection schedule has changed from disabled = {} to {} for universe"
                + " {}",
            scheduleConfig.isDisabled(),
            shouldDisable,
            universe.getUniverseUUID());
        disableSchedule(customer, detectScheduleName, shouldDisable);
      }
      if (detectionInterval.getSeconds() != scheduleConfig.getIntervalSecs()) {
        log.info(
            "Master failover detection schedule has changed from {} secs to {} secs",
            scheduleConfig.getIntervalSecs(),
            detectionInterval.getSeconds());
        jobScheduler.updateSchedule(
            optional.get().getUuid(),
            scheduleConfig.toBuilder().intervalSecs(detectionInterval.getSeconds()).build());
      }
    } else {
      log.info(
          "Creating master failure detection schedule at {} secs for universe {}",
          detectionInterval.getSeconds(),
          universe.getUniverseUUID());
      JobSchedule detectJobSchedule = new JobSchedule();
      detectJobSchedule.setCustomerUuid(customer.getUuid());
      detectJobSchedule.setName(detectScheduleName);
      detectJobSchedule.setScheduleConfig(
          ScheduleConfig.builder()
              .disabled(shouldDisable)
              .intervalSecs(detectionInterval.getSeconds())
              .build());
      detectJobSchedule.setJobConfig(
          FailoverJobConfig.builder()
              .customerUuid(customer.getUuid())
              .universeUuid(universe.getUniverseUUID())
              .failoverJobType(FailoverJobType.DETECT_MASTER_FAILURE)
              .build());
      jobScheduler.submitSchedule(detectJobSchedule);
    }
  }

  private void createMasterFailoverSchedule(Customer customer, Universe universe) {
    String failoverScheduleName = getAutoMasterFailoverScheduleName(universe);
    Optional<JobSchedule> optional =
        jobScheduler.maybeGetSchedule(customer.getUuid(), failoverScheduleName);
    if (optional.isPresent()) {
      boolean shouldDisable = universe.getUniverseDetails().universePaused;
      ScheduleConfig scheduleConfig = optional.get().getScheduleConfig();
      if (shouldDisable && !scheduleConfig.isDisabled()) {
        log.info(
            "Master failover schedule has changed from disabled = {} to {} for universe {}",
            scheduleConfig.isDisabled(),
            shouldDisable,
            universe.getUniverseUUID());
        log.info("Disabling master failover schedule for universe {}", universe.getUniverseUUID());
        disableSchedule(customer, failoverScheduleName, shouldDisable);
      }
    } else {
      JobSchedule failoverJobSchedule = new JobSchedule();
      failoverJobSchedule.setCustomerUuid(customer.getUuid());
      failoverJobSchedule.setName(failoverScheduleName);
      // Schedule is disabled, add a dummy time.
      failoverJobSchedule.setScheduleConfig(
          ScheduleConfig.builder()
              .disabled(true)
              .intervalSecs(Duration.ofDays(1).getSeconds())
              .build());
      failoverJobSchedule.setJobConfig(
          FailoverJobConfig.builder()
              .customerUuid(customer.getUuid())
              .universeUuid(universe.getUniverseUUID())
              .failoverJobType(FailoverJobType.MASTER_FAILOVER)
              .build());
      jobScheduler.submitSchedule(failoverJobSchedule);
    }
  }

  private boolean isFailoverTaskCooldownOver(Customer customer, Universe universe) {
    Optional<CustomerTask> optional =
        CustomerTask.maybeGetLastTaskByTargetUuidTaskType(
            universe.getUniverseUUID(), CustomerTask.TaskType.MasterFailover);
    if (optional.isPresent()) {
      // Cooldown for a new master failover is calculated from the last master failover task.
      Duration cooldownPeriod =
          confGetter.getConfForScope(universe, UniverseConfKeys.autoMasterFailoverCooldown);
      log.debug(
          "Cooldown period is set to {} seconds for universe {}",
          cooldownPeriod.getSeconds(),
          universe.getUniverseUUID());
      Instant restrictionEndTime =
          optional
              .get()
              .getCompletionTime()
              .toInstant()
              .plus(cooldownPeriod.getSeconds(), ChronoUnit.SECONDS);
      Instant now = Instant.now();
      if (restrictionEndTime.isAfter(now)) {
        long diffSecs = now.until(restrictionEndTime, ChronoUnit.SECONDS);
        log.info(
            "Universe {} is cooling down for {} seconds", universe.getUniverseUUID(), diffSecs);
        return false;
      }
    }
    return true;
  }

  // Disabling a schedule already executing a job does not affect the job.
  private void disableSchedule(Customer customer, String scheduleName, boolean disable) {
    jobScheduler
        .maybeGetSchedule(customer.getUuid(), scheduleName)
        .ifPresent(
            s -> {
              if (disable ^ s.getScheduleConfig().isDisabled()) {
                jobScheduler.disableSchedule(s.getUuid(), disable);
                log.info(
                    "Schedule {} has changed from disable={} to {}",
                    scheduleName,
                    s.getScheduleConfig().isDisabled(),
                    disable);
              }
            });
  }

  private void detectMasterFailure(
      Customer customer, Universe universe, RuntimeParams runtimeParams) {
    String failoverScheduleName = getAutoMasterFailoverScheduleName(universe);
    boolean isFailoverEnabled =
        confGetter.getConfForScope(universe, UniverseConfKeys.enableAutoMasterFailover);
    if (!isFailoverEnabled) {
      log.debug(
          "Skipping automated master failover for universe {} because it is disabled",
          universe.getUniverseUUID());
      disableSchedule(customer, failoverScheduleName, true);
      // Let the creator of this schedule handle the life-cycle.
      return;
    }
    if (universe.getUniverseDetails().universePaused) {
      log.info(
          "Skipping master failover for universe {} because it is paused",
          universe.getUniverseUUID());
      disableSchedule(customer, failoverScheduleName, true);
      return;
    }
    if (universe.universeIsLocked()) {
      log.info(
          "Skipping master failover for universe {} because it is already being updated",
          universe.getUniverseUUID());
      disableSchedule(customer, failoverScheduleName, true);
      return;
    }
    Action action =
        autoMasterFailover.getAllowedMasterFailoverAction(
            customer, universe, false /*submitCheck*/);
    if (action.getActionType() == ActionType.NONE) {
      // No fail-over action can be performed.
      disableSchedule(customer, failoverScheduleName, true);
      return;
    }
    log.info(
        "Detected master failure for universe {}, next action {}",
        universe.getUniverseUUID(),
        action);
    if (action.getTaskType() == TaskType.MasterFailover) {
      if (action.getActionType() == ActionType.SUBMIT) {
        if (!isFailoverTaskCooldownOver(customer, universe)) {
          disableSchedule(customer, failoverScheduleName, true);
          return;
        }
        // Verify that the replacement node exists before creating the failover schedule.
        NodeDetails node = universe.getNode(action.getNodeName());
        String possibleReplacementCandidate =
            autoMasterFailover.findReplacementMaster(universe, node, true /* pickNewNode */);
        if (possibleReplacementCandidate == null) {
          disableSchedule(customer, failoverScheduleName, true);
          log.info(
              "Disabling master failover schedule as no replacement master found for node {} in"
                  + " universe {}",
              action.getNodeName(),
              universe.getUniverseUUID());
          return;
        }
        // Reset counters on new submission.
        jobScheduler
            .maybeGetSchedule(customer.getUuid(), failoverScheduleName)
            .ifPresent(s -> jobScheduler.resetCounters(s.getUuid()));
      } else if (action.getActionType() == ActionType.RETRY) {
        long retryLimit =
            (long)
                confGetter.getConfForScope(
                    universe, UniverseConfKeys.autoMasterFailoverMaxTaskRetries);
        if (runtimeParams.getJobSchedule().getFailedCount() > retryLimit) {
          disableSchedule(customer, failoverScheduleName, true);
          String errMsg =
              String.format(
                  "Retry limit of %d reached for task %s on universe %s",
                  retryLimit, action.getTaskType(), universe.getUniverseUUID());
          log.error(errMsg);
          return;
        }
      }
    }
    FailoverJobType jobType =
        action.getTaskType() == TaskType.MasterFailover
            ? FailoverJobType.MASTER_FAILOVER
            : FailoverJobType.SYNC_MASTER_ADDRS;
    log.debug(
        "Task delay is set to {} seconds for universe {}",
        action.getDelay().getSeconds(),
        universe.getUniverseUUID());
    JobSchedule failoverSchedule =
        jobScheduler.getOrBadRequest(customer.getUuid(), failoverScheduleName);
    FailoverJobConfig jobConfig = (FailoverJobConfig) failoverSchedule.getJobConfig();
    ScheduleConfig scheduleConfig = failoverSchedule.getScheduleConfig();
    if (scheduleConfig.isDisabled()) {
      jobScheduler.updateSchedule(
          failoverSchedule.getUuid(),
          scheduleConfig.toBuilder()
              .disabled(false)
              .intervalSecs(action.getDelay().getSeconds())
              .build());
      log.info(
          "Enabled schedule for action {} on universe {} in {} secs",
          action,
          universe.getUniverseUUID(),
          action.getDelay().getSeconds());
    } else {
      // Check if the delay has been updated.
      Duration errorLimit =
          confGetter.getConfForScope(
              universe, UniverseConfKeys.autoMasterFailoverFollowerLagThresholdError);
      Instant nextStartTime = failoverSchedule.getNextStartTime().toInstant();
      Instant newNextStartTime = Instant.now().plus(action.getDelay());
      long diffSecs = Math.abs(nextStartTime.getEpochSecond() - newNextStartTime.getEpochSecond());
      if (diffSecs > errorLimit.getSeconds()) {
        // Update the schedule if the new start time has changed more than the threshold.
        jobScheduler.updateSchedule(
            failoverSchedule.getUuid(),
            scheduleConfig.toBuilder()
                .disabled(false)
                .intervalSecs(action.getDelay().getSeconds())
                .build());
        log.info(
            "Updated schedule for action {} on universe {} in {} secs",
            action,
            universe.getUniverseUUID(),
            action.getDelay().getSeconds());
      }
    }
    if (jobType != jobConfig.getFailoverJobType()) {
      jobScheduler.updateJobConfig(
          failoverSchedule.getUuid(), jobConfig.toBuilder().failoverJobType(jobType).build());
    }
  }
}
