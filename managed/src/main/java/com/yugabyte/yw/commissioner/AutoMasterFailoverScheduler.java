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
    MASTER_FAILOVER
  }

  /** Implementation of the schedule job config for failover. */
  @SuppressWarnings("serial")
  @Getter
  @Builder
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
                log.debug(
                    "Running auto master failover for universe {}", universe.getUniverseUUID());
                AutoMasterFailover automatedMasterFailover =
                    runtime.getInjector().getInstance(AutoMasterFailover.class);
                String detectScheduleName = scheduler.getDetectMasterFailureScheduleName(universe);
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
                      "Disabling master failover schedule for universe {} as detection is disabled",
                      universe.getUniverseUUID());
                  runtime
                      .getJobScheduler()
                      .disableSchedule(runtime.getJobSchedule().getUuid(), true);
                  return;
                }
                automatedMasterFailover
                    .maybeFailoverMaster(customer, universe, runtime)
                    .ifPresent(
                        tf -> {
                          if (tf.getTaskState() == TaskInfo.State.Success) {
                            // Task executed successfully. Disable the schedule for tracking.
                            runtime
                                .getJobScheduler()
                                .disableSchedule(runtime.getJobSchedule().getUuid(), true);
                          } else {
                            // Fail the job and keep the schedule to keep track of the failed
                            // counts.
                            String errMsg =
                                String.format(
                                    "Auto master failover task %s (%s) failed for universe %s",
                                    tf.getTaskType(), tf.getTaskUUID(), universe.getUniverseUUID());
                            throw new RuntimeException(errMsg);
                          }
                        });
                break;
              default:
                throw new RuntimeException("Unknown failover job type " + failoverJobType);
            }
          },
          scheduler.failoverExecutor);
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
                          if (u.getUniverseDetails().universePaused) {
                            log.debug(
                                "Automated master failover for universe {} is paused",
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
                            createDetectMasterFailureSchedule(c, u);
                          } catch (Exception e) {
                            log.error(
                                "Error in creating master failure detection schedule for universe"
                                    + " {} - {}",
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
      log.error("Error in deleting schedules {}", e.getMessage());
    }
  }

  /** Create a schedule to detect master failure for the given universe */
  private void createDetectMasterFailureSchedule(Customer customer, Universe universe) {
    log.trace(
        "Creating master failure detection schedule for universe {} if it is absent",
        universe.getUniverseUUID());
    Duration detectionInterval =
        confGetter.getConfForScope(universe, UniverseConfKeys.autoMasterFailoverDetectionInterval);
    log.debug(
        "Master failover detection interval is set to {} seconds for universe {}",
        detectionInterval.getSeconds(),
        universe.getUniverseUUID());
    String scheduleName = getDetectMasterFailureScheduleName(universe);
    Optional<JobSchedule> optional =
        jobScheduler.maybeGetSchedule(customer.getUuid(), scheduleName);
    if (optional.isPresent()) {
      ScheduleConfig scheduleConfig = optional.get().getScheduleConfig();
      if (scheduleConfig.isDisabled()) {
        log.debug(
            "Master failover detection schedule is disabled for universe {}",
            universe.getUniverseUUID());
        // Disable the existing failover if the detection is disabled.
        String failoverScheduleName = getAutoMasterFailoverScheduleName(universe);
        jobScheduler
            .maybeGetSchedule(customer.getUuid(), failoverScheduleName)
            .ifPresent(
                s -> {
                  if (!s.getScheduleConfig().isDisabled()) {
                    jobScheduler.disableSchedule(s.getUuid(), true);
                  }
                });
      } else if (detectionInterval.getSeconds() != scheduleConfig.getIntervalSecs()) {
        log.info(
            "Failover detection schedule has changed from {} secs to {} secs",
            scheduleConfig.getIntervalSecs(),
            detectionInterval.getSeconds());
        jobScheduler.updateSchedule(
            optional.get().getUuid(),
            scheduleConfig.toBuilder().intervalSecs(detectionInterval.getSeconds()).build());
      }
    } else {
      log.info(
          "Creating master failure detection schedule for universe {}", universe.getUniverseUUID());
      JobSchedule jobSchedule = new JobSchedule();
      jobSchedule.setCustomerUuid(customer.getUuid());
      jobSchedule.setName(scheduleName);
      jobSchedule.setScheduleConfig(
          ScheduleConfig.builder().intervalSecs(detectionInterval.getSeconds()).build());
      jobSchedule.setJobConfig(
          FailoverJobConfig.builder()
              .customerUuid(customer.getUuid())
              .universeUuid(universe.getUniverseUUID())
              .failoverJobType(FailoverJobType.DETECT_MASTER_FAILURE)
              .build());
      jobScheduler.submitSchedule(jobSchedule);
    }
  }

  private boolean isFailoverTaskCooldownOver(Customer customer, Universe universe) {
    Optional<CustomerTask> optional =
        CustomerTask.maybeGetLastTaskByTargetUuidTaskType(
            universe.getUniverseUUID(), CustomerTask.TaskType.MasterFailover);
    if (optional.isPresent()) {
      // Cooldown for a new master failover is calculated from the master failover task.
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
        String scheduleName = getAutoMasterFailoverScheduleName(universe);
        long diffSecs = now.until(restrictionEndTime, ChronoUnit.SECONDS);
        log.info(
            "Universe {} is cooling down for {} seconds", universe.getUniverseUUID(), diffSecs);
        jobScheduler
            .maybeGetSchedule(customer.getUuid(), scheduleName)
            .ifPresent(s -> jobScheduler.disableSchedule(s.getUuid(), true));
        return false;
      }
    }
    return true;
  }

  private void detectMasterFailure(
      Customer customer, Universe universe, RuntimeParams runtimeParams) {
    boolean isFailoverEnabled =
        confGetter.getConfForScope(universe, UniverseConfKeys.enableAutoMasterFailover);
    if (!isFailoverEnabled) {
      log.debug(
          "Skipping automated master failover for universe {} because it is disabled",
          universe.getUniverseUUID(),
          isFailoverEnabled);
      // Let the creator of this schedule handle the life-cycle.
      return;
    }
    if (universe.universeIsLocked()) {
      log.info(
          "Skipping master failover for universe {} because it is already being updated",
          universe.getUniverseUUID());
      // Let the creator of this schedule handle the life-cycle.
      return;
    }
    String scheduleName = getAutoMasterFailoverScheduleName(universe);
    Action action = autoMasterFailover.getAllowedMasterFailoverAction(customer, universe);
    if (action.getActionType() == ActionType.NONE) {
      // No fail-over action can be performed. Disable to keep track of the last run.
      jobScheduler
          .maybeGetSchedule(customer.getUuid(), scheduleName)
          .ifPresent(s -> jobScheduler.disableSchedule(s.getUuid(), true));
      return;
    }
    log.info(
        "Detected master failure for universe {}, next action {}",
        universe.getUniverseUUID(),
        action);
    if (action.getActionType() == ActionType.SUBMIT
        && action.getTaskType() == TaskType.MasterFailover) {
      if (!isFailoverTaskCooldownOver(customer, universe)) {
        return;
      }
      // Verify that the replacement node exists before creating the failover schedule.
      NodeDetails node = universe.getNode(action.getNodeName());
      NodeDetails possibleReplacementCandidate =
          autoMasterFailover.findReplacementMaster(universe, node);
      if (possibleReplacementCandidate == null) {
        log.info(
            "Skipping failover schedule as no replacement master found for node {} in universe {}",
            action.getNodeName(),
            universe.getUniverseUUID());
        return;
      }
    }
    Duration taskDelay =
        action.getTaskType() == TaskType.MasterFailover
            ? confGetter.getConfForScope(universe, UniverseConfKeys.autoMasterFailoverTaskDelay)
            : confGetter.getConfForScope(universe, UniverseConfKeys.autoSyncMasterAddrsTaskDelay);
    log.debug(
        "Task delay is set to {} seconds for universe {}",
        taskDelay.getSeconds(),
        universe.getUniverseUUID());
    Optional<JobSchedule> optional =
        jobScheduler.maybeGetSchedule(customer.getUuid(), scheduleName);
    if (optional.isPresent()) {
      ScheduleConfig scheduleConfig = optional.get().getScheduleConfig();
      if (scheduleConfig.isDisabled()) {
        log.info(
            "Enabled schedule for action {} to perform on universe {} in {} secs",
            action,
            universe.getUniverseUUID(),
            taskDelay.getSeconds());
        jobScheduler.updateSchedule(
            optional.get().getUuid(),
            scheduleConfig.toBuilder()
                .disabled(false)
                .intervalSecs(taskDelay.getSeconds())
                .build());
        log.info(
            "Updated schedule for action {} to perform on universe {} in {} secs",
            action,
            universe.getUniverseUUID(),
            taskDelay.getSeconds());
      }
    } else {
      JobSchedule jobSchedule = new JobSchedule();
      jobSchedule.setCustomerUuid(customer.getUuid());
      jobSchedule.setName(scheduleName);
      jobSchedule.setScheduleConfig(
          ScheduleConfig.builder().intervalSecs(taskDelay.getSeconds()).build());
      jobSchedule.setJobConfig(
          FailoverJobConfig.builder()
              .customerUuid(customer.getUuid())
              .universeUuid(universe.getUniverseUUID())
              .failoverJobType(FailoverJobType.MASTER_FAILOVER)
              .build());
      jobScheduler.submitSchedule(jobSchedule);
      log.info(
          "Scheduled action {} to perform on universe {} in {} seconds",
          action,
          universe.getUniverseUUID(),
          taskDelay.getSeconds());
    }
  }
}
