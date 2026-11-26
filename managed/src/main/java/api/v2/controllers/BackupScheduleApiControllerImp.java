// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.BackupScheduleHandler;
import api.v2.models.BackupSchedule;
import api.v2.models.BackupSchedulePagedQuerySpec;
import api.v2.models.BackupSchedulePagedResp;
import api.v2.models.BackupScheduleSpec;
import api.v2.models.YBATask;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http;

public class BackupScheduleApiControllerImp extends BackupScheduleApiControllerImpInterface {
  private final BackupScheduleHandler backupScheduleHandler;

  @Inject
  public BackupScheduleApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      BackupScheduleHandler backupScheduleHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.backupScheduleHandler = backupScheduleHandler;
  }

  @Override
  public YBATask createBackupSchedule(
      Http.Request request, UUID cUUID, BackupScheduleSpec backupScheduleSpec) {
    return backupScheduleHandler.createBackupSchedule(cUUID, backupScheduleSpec);
  }

  @Override
  public BackupSchedule getBackupSchedule(Http.Request request, UUID cUUID, UUID bsUUID) {
    return backupScheduleHandler.getBackupSchedule(cUUID, bsUUID);
  }

  @Override
  public BackupSchedulePagedResp pageListBackupSchedules(
      Http.Request request, UUID cUUID, BackupSchedulePagedQuerySpec backupSchedulePagedQuerySpec) {
    return backupScheduleHandler.pageListBackupSchedules(cUUID, backupSchedulePagedQuerySpec);
  }
}
