// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.scheduler;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.spy;

import com.google.inject.Injector;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShutdownHookHandler;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.JobInstance;
import com.yugabyte.yw.models.JobSchedule;
import com.yugabyte.yw.models.helpers.schedule.JobConfig;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig.ScheduleType;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import lombok.Getter;
import lombok.Setter;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.mvc.Http.Status;

@RunWith(MockitoJUnitRunner.class)
public class JobSchedulerTest extends FakeDBApplication {
  private Injector injector;
  private ShutdownHookHandler shutdownHookHandler;
  private Customer customer;
  private RuntimeConfGetter confGetter;
  private PlatformExecutorFactory platformExecutorFactory;
  private PlatformScheduler platformScheduler;
  private JobScheduler jobScheduler;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    injector = app.injector().instanceOf(Injector.class);
    shutdownHookHandler = app.injector().instanceOf(ShutdownHookHandler.class);
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    platformExecutorFactory = app.injector().instanceOf(PlatformExecutorFactory.class);
    platformScheduler = app.injector().instanceOf(PlatformScheduler.class);
    jobScheduler =
        spy(
            new JobScheduler(
                injector,
                shutdownHookHandler,
                confGetter,
                platformExecutorFactory,
                platformScheduler));
  }

  @SuppressWarnings("serial")
  @Getter
  @Setter
  static class TestJobConfig implements JobConfig {
    private final AtomicInteger execCount = new AtomicInteger();
    private boolean fail;
    private boolean skip;

    public int getExecCount() {
      return execCount.get();
    }

    @Override
    public CompletableFuture<?> executeJob(RuntimeParams runtimeParams) {
      if (fail) {
        throw new RuntimeException("Failed");
      }
      if (skip) {
        return null;
      }
      execCount.incrementAndGet();
      return CompletableFuture.completedFuture(null);
    }
  }

  @SuppressWarnings("serial")
  static class DummyTestJobConfig implements JobConfig {

    @Override
    public CompletableFuture<?> executeJob(RuntimeParams runtimeParams) {
      return CompletableFuture.completedFuture(null);
    }
  }

  private JobSchedule createJobSchedule(ScheduleConfig scheduleConfig, JobConfig jobConfig) {
    JobSchedule schedule = new JobSchedule();
    schedule.setCustomerUuid(customer.getUuid());
    schedule.setName("test-" + UUID.randomUUID());
    schedule.setScheduleConfig(scheduleConfig);
    schedule.setJobConfig(jobConfig);
    return schedule;
  }

  @Test
  public void testJobScheduleSubmit() {
    ScheduleConfig scheduleConfig = ScheduleConfig.builder().intervalSecs(5).build();
    JobSchedule jobSchedule1 = createJobSchedule(scheduleConfig, new TestJobConfig());
    Set<Class<? extends JobConfig>> submittedJobConfigClasses = new HashSet<>();
    submittedJobConfigClasses.add(TestJobConfig.class);
    submittedJobConfigClasses.add(DummyTestJobConfig.class);
    UUID uuid = jobScheduler.submitSchedule(jobSchedule1);
    JobSchedule dbJobSchedule = JobSchedule.getOrBadRequest(uuid);
    assertEquals(
        jobSchedule1.getScheduleConfig().getIntervalSecs(),
        dbJobSchedule.getScheduleConfig().getIntervalSecs());
    assertEquals(jobSchedule1.getJobConfig().getClass(), dbJobSchedule.getJobConfig().getClass());
    jobScheduler.submitSchedule(createJobSchedule(scheduleConfig, new DummyTestJobConfig()));
    List<JobSchedule> jobSchedules = JobSchedule.getAll();
    assertEquals(2, jobSchedules.size());
    jobSchedules.forEach(
        s -> {
          if (!submittedJobConfigClasses.contains(s.getJobConfig().getClass())) {
            fail("Unknown class " + s.getJobConfig().getClass());
          }
          submittedJobConfigClasses.remove(s.getJobConfig().getClass());
        });
    assertEquals(0, submittedJobConfigClasses.size());
  }

  @Test
  public void testJobScheduleDelete() {
    ScheduleConfig scheduleConfig = ScheduleConfig.builder().intervalSecs(5).build();
    UUID jobScheduleUuid1 =
        jobScheduler.submitSchedule(createJobSchedule(scheduleConfig, new TestJobConfig()));
    UUID jobScheduleUuid2 =
        jobScheduler.submitSchedule(createJobSchedule(scheduleConfig, new DummyTestJobConfig()));
    List<JobSchedule> jobSchedules = JobSchedule.getAll();
    assertEquals(2, jobSchedules.size());
    jobScheduler.deleteSchedule(jobScheduleUuid2);
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class, () -> JobSchedule.getOrBadRequest(jobScheduleUuid2));
    assertEquals(Status.BAD_REQUEST, exception.getHttpStatus());
    jobSchedules = JobSchedule.getAll();
    assertEquals(1, jobSchedules.size());
    JobSchedule jobSchedule1 = JobSchedule.getOrBadRequest(jobScheduleUuid1);
    assertEquals(TestJobConfig.class, jobSchedule1.getJobConfig().getClass());
  }

  @Test
  public void testJobInstanceSuccessExecution() throws Exception {
    ScheduleConfig scheduleConfig =
        ScheduleConfig.builder().type(ScheduleType.FIXED_DELAY).intervalSecs(1).build();
    JobSchedule jobSchedule = createJobSchedule(scheduleConfig, new TestJobConfig());
    UUID uuid = jobScheduler.submitSchedule(jobSchedule);
    JobSchedule dbJobSchedule = JobSchedule.getOrBadRequest(uuid);
    assertEquals(0, dbJobSchedule.getExecutionCount());
    assertEquals(0, dbJobSchedule.getFailedCount());
    assertNotNull(dbJobSchedule.getNextStartTime());
    List<JobInstance> jobInstances = JobInstance.getAll(dbJobSchedule.getUuid());
    assertEquals(1, jobInstances.size());
    JobInstance jobInstance = jobInstances.get(0);
    assertEquals(dbJobSchedule.getNextStartTime(), jobInstance.getStartTime());
    CompletableFuture<?> future = jobScheduler.executeJobInstance(jobInstance);
    assertNotNull(future);
    future.get(10, TimeUnit.SECONDS);
    // Fetch the latest.
    dbJobSchedule = JobSchedule.getOrBadRequest(uuid);
    jobInstance = JobInstance.getOrBadRequest(jobInstance.getUuid());
    assertEquals(1, dbJobSchedule.getExecutionCount());
    assertEquals(0, dbJobSchedule.getFailedCount());
    assertNotNull(dbJobSchedule.getNextStartTime());
    assertNotNull(jobInstance.getEndTime());
    assertTrue(dbJobSchedule.getNextStartTime().after(jobInstance.getEndTime()));
    assertEquals(JobInstance.State.SUCCESS, jobInstance.getState());
  }

  @Test
  public void testJobInstanceFailedExecution() throws Exception {
    ScheduleConfig scheduleConfig =
        ScheduleConfig.builder().type(ScheduleType.FIXED_DELAY).intervalSecs(1).build();
    TestJobConfig jobConfig = new TestJobConfig();
    jobConfig.setFail(true);
    JobSchedule jobSchedule = createJobSchedule(scheduleConfig, jobConfig);
    UUID jobScheduleUuid = jobScheduler.submitSchedule(jobSchedule);
    JobInstance.getAll().forEach(i -> jobScheduler.executeJobInstance(i));
    // Fetch the latest.
    jobSchedule = JobSchedule.getOrBadRequest(jobScheduleUuid);
    List<JobInstance> jobInstances = JobInstance.getAll(jobSchedule.getUuid());
    assertEquals(1, jobInstances.size());
    JobInstance jobInstance = jobInstances.get(0);
    assertEquals(1, jobSchedule.getExecutionCount());
    assertEquals(1, jobSchedule.getFailedCount());
    assertNotNull(jobSchedule.getNextStartTime());
    assertNotNull(jobInstance.getEndTime());
    assertTrue(jobSchedule.getNextStartTime().after(jobInstance.getEndTime()));
    assertEquals(JobInstance.State.FAILED, jobInstance.getState());
  }

  @Test
  public void testJobInstanceSkippedExecution() throws Exception {
    ScheduleConfig scheduleConfig =
        ScheduleConfig.builder().type(ScheduleType.FIXED_DELAY).intervalSecs(1).build();
    TestJobConfig jobConfig = new TestJobConfig();
    jobConfig.setSkip(true);
    JobSchedule jobSchedule = createJobSchedule(scheduleConfig, jobConfig);
    UUID jobScheduleUuid = jobScheduler.submitSchedule(jobSchedule);
    JobInstance.getAll()
        .forEach(
            i -> {
              CompletableFuture<?> future = jobScheduler.executeJobInstance(i);
              assertNull(future);
            });
    // Fetch the latest.
    jobSchedule = JobSchedule.getOrBadRequest(jobScheduleUuid);
    List<JobInstance> jobInstances = JobInstance.getAll(jobSchedule.getUuid());
    assertEquals(1, jobInstances.size());
    JobInstance jobInstance = jobInstances.get(0);
    assertEquals(0, jobSchedule.getExecutionCount());
    assertEquals(0, jobSchedule.getFailedCount());
    assertNotNull(jobSchedule.getNextStartTime());
    assertNotNull(jobInstance.getEndTime());
    assertTrue(jobSchedule.getNextStartTime().after(jobInstance.getEndTime()));
    assertEquals(JobInstance.State.SKIPPED, jobInstance.getState());
  }
}
