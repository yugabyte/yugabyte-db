// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.CreateYbaBackup;
import com.yugabyte.yw.commissioner.tasks.RestoreYbaBackup;
import com.yugabyte.yw.controllers.handlers.YbaBackupHandler;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.YbaBackupForm;
import com.yugabyte.yw.forms.YbaRestoreForm;
import com.yugabyte.yw.models.Customer;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Individual YBA backup management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class YbaBackupController extends AuthenticatedController {

  @Inject private YbaBackupHandler ybaBackupHandler;

  @ApiOperation(nickname = "createYbaBackup", value = "Create an isolated backup of YBA")
  public Result create(UUID customerUUID, Http.Request request) {
    log.info("Received individual yba backup create request");
    YbaBackupForm createForm =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), YbaBackupForm.class);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    CreateYbaBackup.Params taskParams = new CreateYbaBackup.Params();
    taskParams.localDir = createForm.localDir;
    taskParams.components = createForm.components;
    UUID taskUUID = ybaBackupHandler.createBackup(customer, taskParams);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(nickname = "restoreYbaBackup", value = "Restore an isolated backup to YBA")
  public Result restore(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    YbaRestoreForm restoreForm =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), YbaRestoreForm.class);
    RestoreYbaBackup.Params taskParams = new RestoreYbaBackup.Params();
    taskParams.localPath = restoreForm.localPath;
    UUID taskUUID = ybaBackupHandler.restoreBackup(customer, taskParams);
    return new YBPTask(taskUUID).asResult();
  }
}
