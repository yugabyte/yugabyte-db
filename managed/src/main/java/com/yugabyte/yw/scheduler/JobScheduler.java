// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.scheduler;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Injector;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShutdownHookHandler;
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
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.DelayQueue;
import java.util.concurrent.ExecutorService;
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
  private final ShutdownHookHandler shutdownHookHandler;
  private final RuntimeConfGetter confGetter;
  private final PlatformExecutorFactory platformExecutorFactory;
  private final PlatformScheduler platformScheduler;
  private final DelayQueue<JobInstance> jobInstanceQueue;
  // JobSchedule UUID to removable status.
  private final Map<UUID, Boolean> inflightJobSchedules;

  private Duration pollerInterval;
  private Duration scanWindow;
  private Duration jobInstanceRecordTtl;
  private ExecutorService jobInstanceQueueExecutor;

  @Inject
  public JobScheduler(
      Injector injector,
      ShutdownHookHandler shutdownHookHandler,
      RuntimeConfGetter confGetter,
      PlatformExecutorFactory platformExecutorFactory,
      PlatformScheduler platformScheduler) {
    this.injector = injector;
    this.shutdownHookHandler = shutdownHookHandler;
    this.confGetter = confGetter;
    this.platformExecutorFactory = platformExecutorFactory;
    this.platformScheduler = platformScheduler;
    this.jobInstanceQueue = new DelayQueue<>();
    this.inflightJobSchedules = new ConcurrentHashMap<>();
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
    // Override the default handler to shut it down faster.
    shutdownHookHandler.addShutdownHook(
        jobInstanceQueueExecutor,
        exec -> {
          if (exec != null) {
            exec.shutdownNow();
          }
        },
        99 /* weight */);
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
    Date nextTime = createNextStartTime(jobSchedule, true);
    jobSchedule.setNextStartTime(nextTime);
    jobSchedule.save();
    Instant nextMaxPollTime = Instant.now().plus(pollerInterval.getSeconds(), ChronoUnit.SECONDS);
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
   * @return the optional schedule.
   */
  public Optional<JobSchedule> maybeGetSchedule(UUID customerUuid, String name) {
    return JobSchedule.maybeGet(customerUuid, name);
  }

  /**
   * Get the schedule with the name for the given customer if it is found else throw exception.
   *
   * @param customerUuid the customer UUID.
   * @param name the name of the schedule.
   * @return the schedule.
   */
  public JobSchedule getOrBadRequest(UUID customerUuid, String name) {
    return maybeGetSchedule(customerUuid, name)
        .orElseThrow(
            () -> new PlatformServiceException(BAD_REQUEST, "Cannot find job schedule " + name));
  }

  /**
   * Delete the schedule with the given UUID. If the instance is not yet executed, it is deleted.
   *
   * @param uuid the UUID of the schedule.
   */
  public synchronized void deleteSchedule(UUID uuid) {
    JobSchedule.maybeGet(uuid)
        .ifPresent(
            jobSchedule -> {
              jobSchedule.delete();
              if (!removeJobInstanceIfPresent(uuid)) {
                inflightJobSchedules.remove(uuid);
              }
            });
  }

  /**
   * Update the schedule config e.g interval of the schedule.
   *
   * @param uuid the UUID of the schedule.
   * @param scheduleConfig the new schedule config.
   * @return the JobSchedule
   */
  public synchronized JobSchedule updateSchedule(UUID uuid, ScheduleConfig scheduleConfig) {
    Preconditions.checkNotNull(scheduleConfig, "Schedule config must be set");
    JobSchedule jobSchedule = JobSchedule.getOrBadRequest(uuid);
    ScheduleConfig oldScheduleConfig = jobSchedule.getScheduleConfig();
    if (!oldScheduleConfig.equals(scheduleConfig)) {
      jobSchedule.setScheduleConfig(scheduleConfig);
      jobSchedule.setNextStartTime(createNextStartTime(jobSchedule, false));
      jobSchedule.update();
      removeJobInstanceIfPresent(jobSchedule.getUuid());
      Instant nextMaxPollTime = Instant.now().plus(pollerInterval.getSeconds(), ChronoUnit.SECONDS);
      if (nextMaxPollTime.isAfter(jobSchedule.getNextStartTime().toInstant())) {
        // If the next execution time is arriving too soon, add it in the memory as well.
        addJobInstanceIfAbsent(jobSchedule.getUuid());
      }
    }
    return jobSchedule;
  }

  /**
   * Update the job config.
   *
   * @param uuid the UUID of the schedule.
   * @param jobConfig the new job config.
   * @return the JobSchedule
   */
  public synchronized JobSchedule updateJobConfig(UUID uuid, JobConfig jobConfig) {
    Preconditions.checkNotNull(jobConfig, "Job config must be set");
    JobSchedule jobSchedule = JobSchedule.getOrBadRequest(uuid);
    return jobSchedule.updateJobConfig(jobConfig);
  }

  /**
   * Disable or enable the schedule.
   *
   * @param uuid the UUID of the schedule.
   * @param isDisable true to disable else false.
   * @return the updated job schedule.
   */
  public synchronized JobSchedule disableSchedule(UUID uuid, boolean disable) {
    JobSchedule jobSchedule = JobSchedule.getOrBadRequest(uuid);
    return updateSchedule(
        jobSchedule.getUuid(),
        jobSchedule.getScheduleConfig().toBuilder().disabled(disable).build());
  }

  /**
   * Snooze the schedule to start after the given duration.
   *
   * @param uuid the UUID of the schedule.
   * @param snoozeSecs the seconds to snooze.
   * @return the updated job schedule.
   */
  public synchronized JobSchedule snooze(UUID uuid, long snoozeSecs) {
    JobSchedule jobSchedule = JobSchedule.getOrBadRequest(uuid);
    jobSchedule.setScheduleConfig(
        jobSchedule.getScheduleConfig().toBuilder()
            .snoozeUntil(Date.from(Instant.now().plus(snoozeSecs, ChronoUnit.SECONDS)))
            .build());
    jobSchedule.setNextStartTime(createNextStartTime(jobSchedule, true));
    jobSchedule.update();
    removeJobInstanceIfPresent(jobSchedule.getUuid());
    return jobSchedule;
  }

  /**
   * Reset counters like failedCount for the given schedule UUID.
   *
   * @param uuid the schedule UUID.
   * @return the updated job schedule.
   */
  public synchronized JobSchedule resetCounters(UUID uuid) {
    JobSchedule jobSchedule = JobSchedule.getOrBadRequest(uuid);
    return jobSchedule.resetCounters();
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

  @VisibleForTesting
  void handleRestart() {
    // Update dangling instance records on process restart.
    JobInstance.updateAllPending(State.SKIPPED, State.SCHEDULED);
    JobInstance.updateAllPending(State.FAILED);
    // Make currently active schedules inactive.
    JobSchedule.getAll().stream()
        .filter(s -> s.getScheduleConfig().isDisabled() == false)
        .forEach(
            s -> {
              s.setState(JobSchedule.State.INACTIVE);
              s.setNextStartTime(s.getJobConfig().createNextStartTime(s, true));
              s.save();
            });
  }

  // Create the next start time for the schedule. An implementation of JobConfig can choose to
  // override the default behavior.
  private Date createNextStartTime(JobSchedule jobSchedule, boolean restart) {
    return jobSchedule.getJobConfig().createNextStartTime(jobSchedule, restart);
  }

  // Create and add a job instance to the queue when the schedule is picked up for execution soon.
  private synchronized void addJobInstanceIfAbsent(UUID jobScheduleUuid) {
    if (!inflightJobSchedules.containsKey(jobScheduleUuid)) {
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
                inflightJobSchedules.put(s.getUuid(), true /* Removable */);
                jobInstanceQueue.add(jobInstance);
              });
    }
  }

  private synchronized boolean removeJobInstanceIfPresent(UUID jobScheduleUuid) {
    if (inflightJobSchedules.getOrDefault(jobScheduleUuid, false)) {
      // Delete only the records which were found in the queue.
      removeQueuedJobInstances(
              jobInstance -> jobInstance.getJobScheduleUuid().equals(jobScheduleUuid))
          .stream()
          .filter(jobInstance -> jobInstance.getState() == State.SCHEDULED)
          .forEach(JobInstance::delete);
      inflightJobSchedules.remove(jobScheduleUuid);
      return true;
    }
    return false;
  }

  // Return the job instances which were found in the queue.
  private synchronized List<JobInstance> removeQueuedJobInstances(
      Predicate<JobInstance> predicate) {
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
          .filter(uuid -> !inflightJobSchedules.containsKey(uuid))
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
      CompletableFuture<?> future = null;
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
    Optional<JobSchedule> jobScheduleOptional = Optional.empty();
    synchronized (this) {
      jobScheduleOptional = JobSchedule.maybeGet(jobInstance.getJobScheduleUuid());
      if (!jobScheduleOptional.isPresent()) {
        log.warn(
            "Ignoring job {} for schedule {} as it is already deleted",
            jobInstance.getUuid(),
            jobInstance.getJobScheduleUuid());
        return null;
      }
      if (inflightJobSchedules.computeIfPresent(
              jobScheduleOptional.get().getUuid(), (k, v) -> false)
          == null) {
        log.debug(
            "Ignoring job {} for schedule {} as it is already removed",
            jobInstance.getUuid(),
            jobInstance.getJobScheduleUuid());
        return null;
      }
    }
    JobSchedule jobSchedule = jobScheduleOptional.get();
    if (jobInstance.getState() != State.SCHEDULED) {
      updateFinalState(jobSchedule, jobInstance, null);
      log.debug(
          "Skipping job {} for schedule {} as it is not scheduled",
          jobInstance.getUuid(),
          jobInstance.getJobScheduleUuid());
      return null;
    }
    if (jobSchedule.getScheduleConfig().isDisabled()) {
      jobInstance.setState(State.SKIPPED);
      updateFinalState(jobSchedule, jobInstance, null);
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
    }
    return future;
  }

  private synchronized void updateFinalState(
      JobSchedule jobSchedule, JobInstance jobInstance, Throwable t) {
    if (t != null) {
      log.info(
          "Error in excuting job instance {} for schedule {} - {}",
          jobInstance.getUuid(),
          jobSchedule.getUuid(),
          t.getMessage());
    }
    if (inflightJobSchedules.containsKey(jobSchedule.getUuid())) {
      JobSchedule.State jobScheduleState = jobSchedule.getState();
      State jobInstanceState = jobInstance.getState();
      jobSchedule.refresh();
      jobInstance.refresh();
      jobSchedule.setState(jobScheduleState);
      jobInstance.setState(jobInstanceState);
      if (jobInstance.getState() != State.SKIPPED) {
        jobSchedule.setExecutionCount(jobSchedule.getExecutionCount() + 1);
      }
      if (jobInstance.getState() == State.SUCCESS) {
        jobSchedule.setFailedCount(0);
      } else if (jobInstance.getState() == State.FAILED) {
        jobSchedule.setFailedCount(jobSchedule.getFailedCount() + 1);
      }
      jobSchedule.setState(JobSchedule.State.INACTIVE);
      Date endTime = new Date();
      jobSchedule.setLastEndTime(endTime);
      if (!jobSchedule.getScheduleConfig().isDisabled()) {
        jobSchedule.setNextStartTime(createNextStartTime(jobSchedule, false));
      }
      jobSchedule.update();
      jobInstance.setEndTime(endTime);
      jobInstance.update();
      inflightJobSchedules.remove(jobSchedule.getUuid());
    }
    log.debug(
        "Run completed for job instance {} for schedule {} with result {}",
        jobInstance.getUuid(),
        jobSchedule.getUuid(),
        jobInstance.getState());
  }
}
