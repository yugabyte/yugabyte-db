// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.JobSchedulerHandler;
import api.v2.models.JobInstancePagedQuerySpec;
import api.v2.models.JobInstancePagedResp;
import api.v2.models.JobSchedule;
import api.v2.models.JobSchedulePagedQuerySpec;
import api.v2.models.JobSchedulePagedResp;
import api.v2.models.JobScheduleSnoozeSpec;
import api.v2.models.JobScheduleUpdateSpec;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http;
import play.mvc.Http.Request;

public class JobSchedulerApiControllerImp extends JobSchedulerApiControllerImpInterface {
  private final JobSchedulerHandler jobSchedulerHandler;

  @Inject
  public JobSchedulerApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      JobSchedulerHandler jobSchedulerHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.jobSchedulerHandler = jobSchedulerHandler;
  }

  @Override
  public JobSchedulePagedResp pageListJobSchedules(
      Http.Request request, UUID cUUID, JobSchedulePagedQuerySpec jobSchedulePagedQuerySpec) {
    return jobSchedulerHandler.pagedListJobSchedules(cUUID, jobSchedulePagedQuerySpec);
  }

  @Override
  public JobSchedule deleteJobSchedule(Request request, UUID cUUID, UUID jUUID) {
    return jobSchedulerHandler.deleteJobSchedule(cUUID, jUUID);
  }

  @Override
  public JobSchedule getJobSchedule(Request request, UUID cUUID, UUID jUUID) throws Exception {
    return jobSchedulerHandler.getJobSchedule(cUUID, jUUID);
  }

  @Override
  public JobInstancePagedResp pageListJobInstances(
      Request request,
      UUID cUUID,
      UUID jUUID,
      JobInstancePagedQuerySpec jobInstancePagedQuerySpec) {
    return jobSchedulerHandler.pageListJobInstances(cUUID, jUUID, jobInstancePagedQuerySpec);
  }

  @Override
  public JobSchedule snoozeJobSchedule(
      Request request, UUID cUUID, UUID jUUID, JobScheduleSnoozeSpec jobScheduleSnoozeSpec) {
    return jobSchedulerHandler.snoozeJobSchedule(cUUID, jUUID, jobScheduleSnoozeSpec);
  }

  @Override
  public JobSchedule updateJobSchedule(
      Request request, UUID cUUID, UUID jUUID, JobScheduleUpdateSpec jobScheduleUpdateSpec) {
    return jobSchedulerHandler.updateJobSchedule(cUUID, jUUID, jobScheduleUpdateSpec);
  }
}
