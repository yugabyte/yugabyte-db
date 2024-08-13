// Copyright (c) Yugabyte, Inc.

package api.v2.handlers;

import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;

import api.v2.mappers.JobSchedulerMapper;
import api.v2.models.JobInstancePagedQuerySpec;
import api.v2.models.JobInstancePagedResp;
import api.v2.models.JobSchedule;
import api.v2.models.JobSchedulePagedQuerySpec;
import api.v2.models.JobSchedulePagedResp;
import api.v2.models.JobScheduleSnoozeSpec;
import api.v2.models.JobScheduleUpdateSpec;
import api.v2.utils.ApiControllerUtils;
import com.yugabyte.yw.forms.JobScheduleUpdateForm;
import com.yugabyte.yw.models.JobInstance;
import com.yugabyte.yw.models.filters.JobInstanceFilter;
import com.yugabyte.yw.models.filters.JobScheduleFilter;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig;
import com.yugabyte.yw.models.paging.JobInstancePagedQuery;
import com.yugabyte.yw.models.paging.JobInstancePagedResponse;
import com.yugabyte.yw.models.paging.JobSchedulePagedQuery;
import com.yugabyte.yw.models.paging.JobSchedulePagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import com.yugabyte.yw.scheduler.JobScheduler;
import io.ebean.Query;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;

public class JobSchedulerHandler extends ApiControllerUtils {

  private JobScheduler jobScheduler;

  @Inject
  public JobSchedulerHandler(JobScheduler jobScheduler) {
    this.jobScheduler = jobScheduler;
  }

  public JobSchedulePagedResp pagedListJobSchedules(
      UUID customerUuid, JobSchedulePagedQuerySpec pagedQuerySpec) {
    JobSchedulePagedQuery pagedQuery =
        JobSchedulerMapper.INSTANCE.toJobSchedulePagedQuery(pagedQuerySpec);
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(com.yugabyte.yw.models.JobSchedule.SortBy.name);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    if (pagedQuery.getFilter() == null) {
      pagedQuery.setFilter(JobScheduleFilter.builder().build());
    }
    Query<com.yugabyte.yw.models.JobSchedule> query =
        com.yugabyte.yw.models.JobSchedule.createQuery(customerUuid, pagedQuery.getFilter())
            .query();
    JobSchedulePagedResponse response =
        performPagedQuery(query, pagedQuery, JobSchedulePagedResponse.class);
    return JobSchedulerMapper.INSTANCE.toJobSchedulePagedResp(response);
  }

  public JobSchedule getJobSchedule(UUID customerUuid, UUID jobScheduleUuid) {
    return JobSchedulerMapper.INSTANCE.toJobSchedule(
        com.yugabyte.yw.models.JobSchedule.getOrBadRequest(customerUuid, jobScheduleUuid));
  }

  public JobSchedule updateJobSchedule(
      UUID cUUID, UUID jUUID, JobScheduleUpdateSpec jobScheduleUpdateSpec) throws Exception {
    JobScheduleUpdateForm updateForm =
        JobSchedulerMapper.INSTANCE.toJobScheduleUpdateForm(jobScheduleUpdateSpec);
    ScheduleConfig.ScheduleConfigBuilder builder =
        com.yugabyte.yw.models.JobSchedule.getOrBadRequest(cUUID, jUUID)
            .getScheduleConfig()
            .toBuilder();
    builder.intervalSecs(updateForm.intervalSecs);
    builder.disabled(updateForm.disabled);
    builder.type(updateForm.type);
    return JobSchedulerMapper.INSTANCE.toJobSchedule(
        jobScheduler.updateSchedule(jUUID, builder.build()));
  }

  public JobSchedule snoozeJobSchedule(
      UUID cUUID, UUID jUUID, JobScheduleSnoozeSpec jobScheduleSnoozeSpec) throws Exception {
    com.yugabyte.yw.models.JobSchedule jobSchedule =
        com.yugabyte.yw.models.JobSchedule.getOrBadRequest(cUUID, jUUID);
    return JobSchedulerMapper.INSTANCE.toJobSchedule(
        jobScheduler.snooze(jobSchedule.getUuid(), jobScheduleSnoozeSpec.getSnoozeSecs()));
  }

  public JobSchedule deleteJobSchedule(UUID cUUID, UUID jUUID) throws Exception {
    Optional<com.yugabyte.yw.models.JobSchedule> optional =
        com.yugabyte.yw.models.JobSchedule.maybeGet(cUUID, jUUID);
    if (optional.isPresent()) {
      jobScheduler.deleteSchedule(optional.get().getUuid());
      return JobSchedulerMapper.INSTANCE.toJobSchedule(optional.get());
    }
    return new JobSchedule();
  }

  public JobInstancePagedResp pageListJobInstances(
      UUID cUUID, UUID jUUID, JobInstancePagedQuerySpec jobInstancePagedQuerySpec)
      throws Exception {
    com.yugabyte.yw.models.JobSchedule.getOrBadRequest(cUUID, jUUID);
    JobInstancePagedQuery pagedQuery =
        JobSchedulerMapper.INSTANCE.toJobInstancePagedQuery(jobInstancePagedQuerySpec);
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(JobInstance.SortBy.jobScheduleUuid);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    if (pagedQuery.getFilter() == null) {
      pagedQuery.setFilter(JobInstanceFilter.builder().build());
    }
    Query<JobInstance> query = JobInstance.createQuery(jUUID, pagedQuery.getFilter()).query();
    JobInstancePagedResponse response =
        performPagedQuery(query, pagedQuery, JobInstancePagedResponse.class);
    return JobSchedulerMapper.INSTANCE.toJobInstancePagedResp(response);
  }
}
