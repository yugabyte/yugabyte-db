// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.scheduler;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Injector;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.JobInstance;
import com.yugabyte.yw.models.JobInstance.State;
import com.yugabyte.yw.models.JobSchedule;
import com.yugabyte.yw.models.helpers.schedule.JobConfig;
import com.yugabyte.yw.models.helpers.schedule.JobConfig.RuntimeParams;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Date;
import java.util.Iterator;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.DelayQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * A generic job scheduler for running background tasks at intervals. A job must implement
 * JobConfig @see com.yugabyte.yw.models.helpers.schedule.JobConfig. Each job schedule creates the
 * next job instance when it is scheduled to execute soon.
 */
@Slf4j
@Singleton
public class JobScheduler {
  private static final Duration POLLER_INITIAL_DELAY = Duration.ofMinutes(1);
  private static final Duration DEFAULT_POLLER_INTERVAL = Duration.ofMinutes(1);
  private static final String JOB_SCHEDULE_POLLER_INTERVAL = "yb.job_scheduler.poller_interval";
  private static final String JOB_SCHEDULE_INSTANCE_RECORD_TTL =
      "yb.job_scheduler.instance_record_ttl";

  private final Injector injector;
  private final RuntimeConfGetter confGetter;
  private final PlatformExecutorFactory platformExecutorFactory;
  private final PlatformScheduler platformScheduler;
  private final DelayQueue<JobInstance> jobInstanceQueue;
  private final Set<UUID> inflightJobSchedules;

  private Duration pollerInterval;
  private Duration scanWindow;
  private Duration jobInstanceRecordTtl;
  private ExecutorService jobInstanceQueueExecutor;

  @Inject
  public JobScheduler(
      Injector injector,
      RuntimeConfGetter confGetter,
      PlatformExecutorFactory platformExecutorFactory,
      PlatformScheduler platformScheduler) {
    this.injector = injector;
    this.confGetter = confGetter;
    this.platformExecutorFactory = platformExecutorFactory;
    this.platformScheduler = platformScheduler;
    this.jobInstanceQueue = new DelayQueue<>();
    this.inflightJobSchedules = ConcurrentHashMap.newKeySet();
    this.pollerInterval = DEFAULT_POLLER_INTERVAL;
  }

  public void init() {
    pollerInterval = confGetter.getStaticConf().getDuration(JOB_SCHEDULE_POLLER_INTERVAL);
    if (pollerInterval.isZero()) {
      throw new IllegalArgumentException(
          String.format("%s must be greater than 0", JOB_SCHEDULE_POLLER_INTERVAL));
    }
    jobInstanceRecordTtl = confGetter.getStaticConf().getDuration(JOB_SCHEDULE_INSTANCE_RECORD_TTL);
    if (jobInstanceRecordTtl.isZero()) {
      throw new IllegalArgumentException(
          String.format("%s must be greater than 0", JOB_SCHEDULE_INSTANCE_RECORD_TTL));
    }
    scanWindow = pollerInterval.plus(pollerInterval);
    handleRestart();

    jobInstanceQueueExecutor =
        platformExecutorFactory.createFixedExecutor(
            JobInstance.class.getSimpleName(),
            1,
            new ThreadFactoryBuilder().setNameFormat("job-instance-%d").build());
    jobInstanceQueueExecutor.submit(this::processJobInstanceQueue);
    platformScheduler.schedule(
        JobScheduler.class.getSimpleName(),
        POLLER_INITIAL_DELAY,
        pollerInterval,
        () -> poll(scanWindow));
  }

  /**
   * Submit the job schedule to the scheduler.
   *
   * @param jobSchedule the schedule.
   * @return the UUID of the schedule.
   */
  public UUID submitSchedule(JobSchedule jobSchedule) {
    Preconditions.checkNotNull(jobSchedule, "Job schedule must be set");
    Preconditions.checkNotNull(jobSchedule.getCustomerUuid(), "Customer UUID must be set");
    Preconditions.checkArgument(StringUtils.isNotEmpty(jobSchedule.getName()), "Name must be set");
    Preconditions.checkNotNull(jobSchedule.getCustomerUuid(), "Customer UUID must be set");
    Preconditions.checkNotNull(jobSchedule.getScheduleConfig(), "Schedule config must be set");
    Preconditions.checkNotNull(jobSchedule.getJobConfig(), "Job config must be set");
    Instant nextMaxPollTime = Instant.now().plus(pollerInterval.getSeconds(), ChronoUnit.SECONDS);
    Date nextTime = createNextStartTime(jobSchedule);
    jobSchedule.setNextStartTime(nextTime);
    jobSchedule.save();
    if (nextMaxPollTime.isAfter(nextTime.toInstant())) {
      // If the next execution time is arriving too soon, add it in the memory as well.
      addJobInstanceIfAbsent(jobSchedule.getUuid());
    }
    return jobSchedule.getUuid();
  }

  /**
   * Get the schedule with the given UUID if it is found.
   *
   * @param uuid the given UUID of the schedule.
   * @return the optional schedule.
   */
  public Optional<JobSchedule> maybeGetSchedule(UUID uuid) {
    return JobSchedule.maybeGet(uuid);
  }

  /**
   * Get the schedule with the name for the given customer if it is found.
   *
   * @param customerUuid the customer UUID.
   * @param name the name of the schedule.
   * @return
   */
  public Optional<JobSchedule> maybeGetSchedule(UUID customerUuid, String name) {
    return JobSchedule.maybeGet(customerUuid, name);
  }

  /**
   * Delete the schedule with the given UUID. If the instance is not yet executed, it is deleted.
   *
   * @param uuid the UUID of the schedule.
   */
  public void deleteSchedule(UUID uuid) {
    JobSchedule.maybeGet(uuid)
        .ifPresent(
            jobSchedule -> {
              jobSchedule.delete();
              removeJobInstanceIfPresent(uuid);
            });
  }

  /**
   * Update the schedule config e.g interval of the schedule.
   *
   * @param uuid the UUID of the schedule.
   * @param scheduleConfig the new schedule config.
   */
  public void updateSchedule(UUID uuid, ScheduleConfig scheduleConfig) {
    Preconditions.checkNotNull(scheduleConfig, "Schedule config must be set");
    JobSchedule jobSchedule = JobSchedule.getOrBadRequest(uuid);
    jobSchedule.updateScheduleConfig(scheduleConfig);
    removeJobInstanceIfPresent(uuid);
  }

  /**
   * Disable or enable the schedule.
   *
   * @param uuid the UUID of the schedule.
   * @param isDisable true to disable else false.
   */
  public void disableSchedule(UUID uuid, boolean disable) {
    JobSchedule jobSchedule = JobSchedule.getOrBadRequest(uuid);
    jobSchedule.updateScheduleConfig(
        jobSchedule.getScheduleConfig().toBuilder().disabled(disable).build());
  }

  /**
   * Delete schedules for the given JobConfig class that also satisfy the predicate.
   *
   * @param jobConfigClass the job config class.
   * @param predicate the predicate to test.
   */
  public void deleteSchedulesIf(
      Class<? extends JobConfig> jobConfigClass, Predicate<JobSchedule> predicate) {
    JobSchedule.getAll(jobConfigClass).stream()
        .filter(s -> predicate == null || predicate.test(s))
        .forEach(s -> deleteSchedule(s.getUuid()));
  }

  // Update dangling instance records on process restart.
  private void handleRestart() {
    JobInstance.updateAllPending(State.SKIPPED, State.SCHEDULED);
    JobInstance.updateAllPending(State.FAILED);
  }

  // Create the next start time for the schedule. An implementation of JobConfig can choose to
  // override the default behavior.
  private Date createNextStartTime(JobSchedule jobSchedule) {
    return jobSchedule.getJobConfig().createNextStartTime(jobSchedule);
  }

  // Create and add a job instance to the queue when the schedule is picked up for execution soon.
  private synchronized void addJobInstanceIfAbsent(UUID jobScheduleUuid) {
    if (!inflightJobSchedules.contains(jobScheduleUuid)) {
      // Get the record again if the UUID was fetched right before the record deletion but after the
      // removal from the in-flight tracker.
      JobSchedule.maybeGet(jobScheduleUuid)
          .ifPresent(
              s -> {
                log.info("Adding job instance for schedule {}", s.getUuid());
                JobInstance jobInstance = new JobInstance();
                jobInstance.setJobScheduleUuid(s.getUuid());
                jobInstance.setStartTime(s.getNextStartTime());
                jobInstance.save();
                inflightJobSchedules.add(s.getUuid());
                jobInstanceQueue.add(jobInstance);
              });
    }
  }

  private synchronized void removeJobInstanceIfPresent(UUID jobScheduleUuid) {
    if (inflightJobSchedules.contains(jobScheduleUuid)) {
      // Delete only the records which were found in the queue.
      removeQueuedJobInstances(
              jobInstance -> jobInstance.getJobScheduleUuid().equals(jobScheduleUuid))
          .stream()
          .forEach(JobInstance::delete);
      inflightJobSchedules.remove(jobScheduleUuid);
    }
  }

  // Return the job instances which were found in the queue.
  private List<JobInstance> removeQueuedJobInstances(Predicate<JobInstance> predicate) {
    List<JobInstance> tobeRemovedJobInstances = new ArrayList<>();
    Iterator<JobInstance> iter = jobInstanceQueue.iterator();
    while (iter.hasNext()) {
      JobInstance jobInstance = iter.next();
      if (predicate.test(jobInstance)) {
        tobeRemovedJobInstances.add(jobInstance);
      }
    }
    // Collect the job instances which are removed before they are dequeued.
    return tobeRemovedJobInstances.stream()
        .filter(j -> jobInstanceQueue.remove(j))
        .collect(Collectors.toList());
  }

  private void poll(Duration scanWindow) {
    try {
      JobSchedule.getNextEnabled(scanWindow).stream()
          .filter(uuid -> !inflightJobSchedules.contains(uuid))
          .forEach(
              uuid -> {
                try {
                  addJobInstanceIfAbsent(uuid);
                } catch (Exception e) {
                  log.error(
                      "Exception occurred in handling schedule {} - {}", uuid, e.getMessage());
                }
              });
      int deletedCount = JobInstance.deleteExpired(jobInstanceRecordTtl);
      log.debug("Expired {} job instances", deletedCount);
    } catch (Exception e) {
      log.error("Exception occurred in scanning schedules - {}", e.getMessage());
    }
  }

  private void processJobInstanceQueue() {
    while (!jobInstanceQueueExecutor.isShutdown()) {
      Future<?> future = null;
      JobInstance jobInstance = null;
      try {
        jobInstance = jobInstanceQueue.take();
        future = executeJobInstance(jobInstance);
      } catch (InterruptedException e) {
        log.debug("Job instance dequeuer interuppted");
      } catch (Exception e) {
        log.error("Exception occurred in job instance execution - {}", e.getMessage());
      } finally {
        if (future == null && jobInstance != null) {
          inflightJobSchedules.remove(jobInstance.getJobScheduleUuid());
        }
      }
    }
  }

  private synchronized void finalizeJob(JobSchedule jobSchedule, JobInstance jobInstance) {
    if (inflightJobSchedules.contains(jobSchedule.getUuid())) {
      Date endTime = new Date();
      jobSchedule.setLastEndTime(endTime);
      jobInstance.setEndTime(endTime);
      jobSchedule.setNextStartTime(createNextStartTime(jobSchedule));
      jobSchedule.update();
      jobInstance.update();
      inflightJobSchedules.remove(jobSchedule.getUuid());
    }
  }

  @VisibleForTesting
  CompletableFuture<?> executeJobInstance(JobInstance instance) {
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping scheduled job execution on HA standby node");
      return null;
    }
    Optional<JobInstance> jobInstanceOptional = JobInstance.maybeGet(instance.getUuid());
    if (!jobInstanceOptional.isPresent()) {
      log.debug(
          "Job instance {} is not found for schedule {}",
          instance.getUuid(),
          instance.getJobScheduleUuid());
      return null;
    }
    JobInstance jobInstance = jobInstanceOptional.get();
    JobSchedule jobSchedule = JobSchedule.getOrBadRequest(jobInstance.getJobScheduleUuid());
    if (jobInstance.getState() != State.SCHEDULED) {
      finalizeJob(jobSchedule, jobInstance);
      log.debug(
          "Skipping job {} for schedule {} as it is not scheduled",
          jobInstance.getUuid(),
          jobInstance.getJobScheduleUuid());
      return null;
    }
    if (jobSchedule.getScheduleConfig().isDisabled()) {
      jobInstance.setState(State.SKIPPED);
      finalizeJob(jobSchedule, jobInstance);
      log.debug(
          "Skipping job {} as the schedule {} is disabled",
          jobInstance.getUuid(),
          jobInstance.getJobScheduleUuid());
      return null;
    }
    log.debug(
        "Executing job instance {} for schedule {}",
        jobInstance.getUuid(),
        jobInstance.getJobScheduleUuid());
    JobConfig jobConfig = jobSchedule.getJobConfig();
    CompletableFuture<?> future = null;
    try {
      jobSchedule.setState(JobSchedule.State.ACTIVE);
      jobSchedule.setLastJobInstanceUuid(jobInstance.getUuid());
      jobSchedule.setLastStartTime(new Date());
      jobSchedule.update();
      jobInstance.setState(State.RUNNING);
      jobInstance.update();
      RuntimeParams runtime =
          RuntimeParams.builder()
              .injector(injector)
              .confGetter(confGetter)
              .jobScheduler(JobScheduler.this)
              .jobSchedule(jobSchedule)
              .jobInstance(jobInstance)
              .build();
      future = jobConfig.executeJob(runtime);
      if (future == null) {
        jobInstance.setState(State.SKIPPED);
        updateFinalState(jobSchedule, jobInstance, null);
      } else {
        future =
            future.handle(
                (o, e) -> {
                  jobInstance.setState(e == null ? State.SUCCESS : State.FAILED);
                  updateFinalState(jobSchedule, jobInstance, e);
                  return o;
                });
      }
    } catch (Exception e) {
      jobInstance.setState(State.FAILED);
      updateFinalState(jobSchedule, jobInstance, e);
    } finally {
      log.debug(
          "Run completed for job instance {} for schedule {} with result {}",
          jobInstance.getUuid(),
          jobSchedule.getUuid(),
          jobInstance.getState());
    }
    return future;
  }

  private void updateFinalState(JobSchedule jobSchedule, JobInstance jobInstance, Throwable t) {
    if (t != null) {
      log.info(
          "Error in excuting kob instance {} for schedule {} - {}",
          jobInstance.getUuid(),
          jobSchedule.getUuid(),
          t.getMessage());
    }
    if (jobInstance.getState() != State.SKIPPED) {
      jobSchedule.setExecutionCount(jobSchedule.getExecutionCount() + 1);
    }
    if (jobInstance.getState() == State.SUCCESS) {
      jobSchedule.setFailedCount(0);
    } else if (jobInstance.getState() == State.FAILED) {
      jobSchedule.setFailedCount(jobSchedule.getFailedCount() + 1);
    }
    jobSchedule.setState(JobSchedule.State.INACTIVE);
    finalizeJob(jobSchedule, jobInstance);
  }
}
