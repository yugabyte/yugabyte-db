// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.is;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.JobSchedulerApi;
import com.yugabyte.yba.v2.client.models.JobInstanceApiFilter;
import com.yugabyte.yba.v2.client.models.JobInstancePagedQuerySpec;
import com.yugabyte.yba.v2.client.models.JobInstancePagedResp;
import com.yugabyte.yba.v2.client.models.JobSchedule;
import com.yugabyte.yba.v2.client.models.JobScheduleApiFilter;
import com.yugabyte.yba.v2.client.models.JobSchedulePagedQuerySpec;
import com.yugabyte.yba.v2.client.models.JobSchedulePagedResp;
import com.yugabyte.yba.v2.client.models.JobScheduleSnoozeSpec;
import com.yugabyte.yba.v2.client.models.JobScheduleType;
import com.yugabyte.yba.v2.client.models.JobScheduleUpdateSpec;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.schedule.JobConfig;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig.ScheduleType;
import com.yugabyte.yw.scheduler.JobScheduler;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import lombok.Builder;
import lombok.Getter;
import lombok.extern.jackson.Jacksonized;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class JobSchedulerControllerTest extends FakeDBApplication {
  private JobSchedulerApi jobSchedulerApi;
  private JobScheduler jobScheduler;
  private Customer customer;
  private Users user;
  private String authToken;

  @SuppressWarnings("serial")
  @Getter
  @Builder
  @Jacksonized
  public static class DummyJobConfig implements JobConfig {
    private String field;

    @Override
    public CompletableFuture<?> executeJob(RuntimeParams runtimeParams) {
      return CompletableFuture.completedFuture(null);
    }
  }

  @Before
  public void setUp() {
    jobScheduler = app.injector().instanceOf(JobScheduler.class);
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();
    ApiClient v2Client = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2Client = v2Client.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", authToken);
    Configuration.setDefaultApiClient(v2Client);
    jobSchedulerApi = new JobSchedulerApi();
  }

  @After
  public void tearDown() {
    com.yugabyte.yw.models.JobSchedule.getAll().forEach(j -> j.delete());
  }

  private com.yugabyte.yw.models.JobSchedule createJobSchedule(
      String name, long intervalSecs, boolean disabled) {
    com.yugabyte.yw.models.JobSchedule jobSchedule = new com.yugabyte.yw.models.JobSchedule();
    jobSchedule.setCustomerUuid(customer.getUuid());
    jobSchedule.setName(name);
    jobSchedule.setScheduleConfig(
        ScheduleConfig.builder().disabled(disabled).intervalSecs(intervalSecs).build());
    jobSchedule.setJobConfig(DummyJobConfig.builder().field(name).build());
    return com.yugabyte.yw.models.JobSchedule.getOrBadRequest(
        jobScheduler.submitSchedule(jobSchedule));
  }

  private com.yugabyte.yw.models.JobInstance createJobInstance(UUID jobScheduleUuid) {
    com.yugabyte.yw.models.JobSchedule jobSchedule =
        com.yugabyte.yw.models.JobSchedule.getOrBadRequest(jobScheduleUuid);
    com.yugabyte.yw.models.JobInstance jobInstance = new com.yugabyte.yw.models.JobInstance();
    jobInstance.setJobScheduleUuid(jobSchedule.getUuid());
    jobInstance.setStartTime(jobSchedule.getNextStartTime());
    jobInstance.insert();
    return jobInstance;
  }

  private List<com.yugabyte.yw.models.JobSchedule> createJobSchedules(int count) {
    return IntStream.range(0, count)
        .mapToObj(i -> createJobSchedule("test_" + i, 180, false))
        .collect(Collectors.toList());
  }

  @Test
  public void testPageListJobSchedules() throws ApiException {
    createJobSchedules(3);
    JobScheduleApiFilter filter = new JobScheduleApiFilter();
    JobSchedulePagedQuerySpec query = new JobSchedulePagedQuerySpec();
    query.setSortBy(JobSchedulePagedQuerySpec.SortByEnum.NAME);
    query.setDirection(JobSchedulePagedQuerySpec.DirectionEnum.DESC);
    query.setLimit(2);
    query.setOffset(1);
    query.setFilter(filter);
    JobSchedulePagedResp jobSchedules =
        jobSchedulerApi.pageListJobSchedules(customer.getUuid(), query);
    assertThat(jobSchedules.getHasNext(), is(false));
    assertThat(jobSchedules.getHasPrev(), is(true));
    assertThat(jobSchedules.getTotalCount(), equalTo(3));
    assertThat(jobSchedules.getEntities(), hasSize(2));
  }

  @Test
  public void testGetJobSchedule() throws ApiException {
    List<com.yugabyte.yw.models.JobSchedule> jobSchedules = createJobSchedules(3);
    JobSchedule jobSchedule =
        jobSchedulerApi.getJobSchedule(customer.getUuid(), jobSchedules.get(0).getUuid());
    assertEquals(jobSchedules.get(0).getUuid(), jobSchedule.getInfo().getUuid());
    assertEquals(jobSchedules.get(0).getName(), jobSchedule.getInfo().getName());
    assertEquals(
        jobSchedules.get(0).getScheduleConfig().getType().name(),
        jobSchedule.getSpec().getScheduleConfig().getType().getValue());
    assertEquals(
        jobSchedules.get(0).getScheduleConfig().getIntervalSecs(),
        jobSchedule.getSpec().getScheduleConfig().getIntervalSecs().longValue());
    assertEquals(
        jobSchedules.get(0).getScheduleConfig().isDisabled(),
        jobSchedule.getSpec().getScheduleConfig().getDisabled());
    assertEquals(
        jobSchedules.get(0).getJobConfig().getClass().getName(),
        jobSchedule.getSpec().getJobConfig().getClassname());
  }

  @Test
  public void testUpdateJobSchedule() throws ApiException {
    List<com.yugabyte.yw.models.JobSchedule> jobSchedules = createJobSchedules(2);
    com.yugabyte.yw.models.JobSchedule jobSchedule = jobSchedules.get(0);
    JobScheduleUpdateSpec updateSpec = new JobScheduleUpdateSpec();
    // Non-default type.
    updateSpec.setType(JobScheduleType.DELAY);
    updateSpec.setIntervalSecs(600L);
    updateSpec.setDisabled(false);
    JobSchedule updatedJobSchedule =
        jobSchedulerApi.updateJobSchedule(customer.getUuid(), jobSchedule.getUuid(), updateSpec);
    assertEquals(jobSchedule.getUuid(), jobSchedule.getUuid());
    assertEquals(
        600, updatedJobSchedule.getSpec().getScheduleConfig().getIntervalSecs().longValue());
    // Verify in DB as well.
    com.yugabyte.yw.models.JobSchedule updatedJobScheduleDb =
        com.yugabyte.yw.models.JobSchedule.getOrBadRequest(jobSchedule.getUuid());
    assertEquals(jobSchedule.getUuid(), updatedJobScheduleDb.getUuid());
    assertEquals(ScheduleType.FIXED_DELAY, updatedJobScheduleDb.getScheduleConfig().getType());
    assertEquals(600, updatedJobScheduleDb.getScheduleConfig().getIntervalSecs());
    // Other one must not get updated.
    com.yugabyte.yw.models.JobSchedule otherJobSchedule =
        com.yugabyte.yw.models.JobSchedule.getOrBadRequest(jobSchedules.get(1).getUuid());
    assertEquals(ScheduleType.FIXED_DELAY, otherJobSchedule.getScheduleConfig().getType());
    assertEquals(180, otherJobSchedule.getScheduleConfig().getIntervalSecs());
  }

  @Test
  public void testSnoozeJobSchedule() throws ApiException {
    List<com.yugabyte.yw.models.JobSchedule> jobSchedules = createJobSchedules(2);
    com.yugabyte.yw.models.JobSchedule jobSchedule = jobSchedules.get(0);
    JobScheduleSnoozeSpec snoozeForm = new JobScheduleSnoozeSpec();
    snoozeForm.setSnoozeSecs(605L);
    JobSchedule updatedJobSchedule =
        jobSchedulerApi.snoozeJobSchedule(customer.getUuid(), jobSchedule.getUuid(), snoozeForm);
    assertEquals(jobSchedule.getUuid(), jobSchedule.getUuid());
    assertEquals(
        180, updatedJobSchedule.getSpec().getScheduleConfig().getIntervalSecs().longValue());
    assertTrue(
        updatedJobSchedule
            .getInfo()
            .getNextStartTime()
            .isAfter(OffsetDateTime.now().plus(10L, ChronoUnit.MINUTES)));
    // Verify in DB as well.
    com.yugabyte.yw.models.JobSchedule updatedJobScheduleDb =
        com.yugabyte.yw.models.JobSchedule.getOrBadRequest(jobSchedule.getUuid());
    assertEquals(jobSchedule.getUuid(), updatedJobScheduleDb.getUuid());
    assertEquals(180, updatedJobScheduleDb.getScheduleConfig().getIntervalSecs());
    assertTrue(
        updatedJobScheduleDb
            .getNextStartTime()
            .after(Date.from(Instant.now().plus(10L, ChronoUnit.MINUTES))));
    // Other one must not get updated.
    com.yugabyte.yw.models.JobSchedule otherJobSchedule =
        com.yugabyte.yw.models.JobSchedule.getOrBadRequest(jobSchedules.get(1).getUuid());
    assertEquals(180, otherJobSchedule.getScheduleConfig().getIntervalSecs());
    assertTrue(
        otherJobSchedule
            .getNextStartTime()
            .before(Date.from(Instant.now().plus(10L, ChronoUnit.MINUTES))));
  }

  @Test
  public void testPageListJobInstances() throws ApiException {
    com.yugabyte.yw.models.JobSchedule jobSchedule = createJobSchedules(1).get(0);
    createJobInstance(jobSchedule.getUuid());
    createJobInstance(jobSchedule.getUuid());
    createJobInstance(jobSchedule.getUuid());
    JobInstanceApiFilter filter = new JobInstanceApiFilter();
    JobInstancePagedQuerySpec query = new JobInstancePagedQuerySpec();
    query.setSortBy(JobInstancePagedQuerySpec.SortByEnum.JOBSCHEDULEUUID);
    query.setDirection(JobInstancePagedQuerySpec.DirectionEnum.DESC);
    query.setLimit(2);
    query.setOffset(1);
    query.setFilter(filter);
    JobInstancePagedResp jobInstances =
        jobSchedulerApi.pageListJobInstances(customer.getUuid(), jobSchedule.getUuid(), query);
    assertThat(jobInstances.getHasNext(), is(false));
    assertThat(jobInstances.getHasPrev(), is(true));
    assertThat(jobInstances.getTotalCount(), equalTo(3));
    assertThat(jobInstances.getEntities(), hasSize(2));
  }
}
