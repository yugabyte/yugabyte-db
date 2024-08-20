// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.RestoreContinuousBackup;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.handlers.YbaBackupHandler;
import com.yugabyte.yw.forms.ContinuousBackupForm;
import com.yugabyte.yw.forms.ContinuousBackupGetResp;
import com.yugabyte.yw.forms.ContinuousRestoreForm;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import com.yugabyte.yw.models.Customer;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Continuous YBA backup management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class ContinuousBackupController extends AuthenticatedController {

  @Inject private YbaBackupHandler ybaBackupHandler;

  @ApiOperation(
      nickname = "createContinuousBackupConfig",
      value = "Create continuous backup config")
  public Result create(UUID customerUUID, Http.Request request) {
    log.info("Received continuous backup config create request.");
    ContinuousBackupForm createForm =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), ContinuousBackupForm.class);
    ContinuousBackupConfig cbConfig =
        ContinuousBackupConfig.create(
            createForm.storageConfigUUID,
            createForm.frequency,
            createForm.frequencyTimeUnit,
            createForm.numBackupsToRetain,
            createForm.backupDir);
    ContinuousBackupGetResp cbConfigResp = new ContinuousBackupGetResp(cbConfig);
    return PlatformResults.withData(cbConfigResp);
  }

  @ApiOperation(nickname = "editContinuousBackupConfig", value = "Edit continuous backup config")
  public Result edit(UUID customerUUID, UUID cbConfigUuid, Http.Request request) {
    log.info("Received edit continuous backup config request.");
    ContinuousBackupForm editForm =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), ContinuousBackupForm.class);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      nickname = "getContinuousBackupConfig",
      value = "Get continuous backup config details",
      response = ContinuousBackupGetResp.class)
  public Result get(UUID customerUUID, Http.Request request) {
    log.info("Received get continuous backup request.");
    List<ContinuousBackupConfig> cbConfigs = ContinuousBackupConfig.getAll();
    if (cbConfigs.size() < 1) {
      log.warn("Could not find any continuous backup config.");
      throw new PlatformServiceException(NOT_FOUND, "No continuous backup config found.");
    }
    if (cbConfigs.size() > 1) {
      log.warn(
          "Found multiple backup configs, which should not be possible. Continuing with first"
              + " found.");
    }
    ContinuousBackupConfig cbConfig = cbConfigs.get(0);
    ContinuousBackupGetResp cbConfigResp = new ContinuousBackupGetResp(cbConfig);
    return PlatformResults.withData(cbConfigResp);
  }

  @ApiOperation(
      nickname = "deleteContinuousBackupConfig",
      value = "Delete continuous backup config")
  public Result delete(UUID customerUUID, UUID cbConfigUUID, Http.Request request) {
    log.info("Received delete continuous backup request.");
    Optional<ContinuousBackupConfig> cbConfig = ContinuousBackupConfig.get(cbConfigUUID);
    if (!cbConfig.isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No continous backup config found with UUID.");
    }
    ContinuousBackupConfig.delete(cbConfigUUID);
    return YBPSuccess.empty();
  }

  @ApiOperation(nickname = "restoreContinuousBackup", value = "Restore from continuous backup")
  public Result restore(UUID customerUUID, Http.Request request) {
    log.info("Received continuous backup restore request.");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    ContinuousRestoreForm restoreForm =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), ContinuousRestoreForm.class);
    RestoreContinuousBackup.Params taskParams = new RestoreContinuousBackup.Params();
    taskParams.storageConfigUUID = restoreForm.storageConfigUUID;
    taskParams.backupDir = restoreForm.backupDir;
    UUID taskUUID = ybaBackupHandler.restoreContinuousBackup(customer, taskParams);
    return new YBPTask(taskUUID).asResult();
  }
}
