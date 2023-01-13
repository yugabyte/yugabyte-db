// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.RunApiTriggeredHooks;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.HookRequestData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.IOUtils;
import play.data.Form;
import play.libs.Json;
import play.mvc.Http.MultipartFormData;
import play.mvc.Http.MultipartFormData.FilePart;
import play.mvc.Result;

@Slf4j
@Api(
    value = "Hook Management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH),
    hidden = true)
public class HookController extends AuthenticatedController {

  public static final String ENABLE_CUSTOM_HOOKS_PATH =
      "yb.security.custom_hooks.enable_custom_hooks";
  public static final String ENABLE_SUDO_PATH = "yb.security.custom_hooks.enable_sudo";
  public static final String ENABLE_API_HOOK_RUN_PATH =
      "yb.security.custom_hooks.enable_api_triggered_hooks";

  @Inject private TokenAuthenticator tokenAuthenticator;

  @Inject RuntimeConfigFactory rConfigFactory;
  @Inject Commissioner commissioner;

  @ApiOperation(
      value = "List all hooks",
      nickname = "listHooks",
      response = Hook.class,
      responseContainer = "List")
  public Result list(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer);
    List<Hook> hooks = Hook.getAll(customerUUID);
    return PlatformResults.withData(hooks);
  }

  @ApiOperation(value = "Create a Hook", nickname = "createHook", response = Hook.class)
  public Result create(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer);
    Form<HookRequestData> formData = formFactory.getFormDataOrBadRequest(HookRequestData.class);
    HookRequestData form = formData.get();
    boolean isSudoEnabled = rConfigFactory.globalRuntimeConf().getBoolean(ENABLE_SUDO_PATH);
    form.verify(customerUUID, true, isSudoEnabled);

    MultipartFormData<File> multiPartBody = request().body().asMultipartFormData();
    if (multiPartBody == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No custom hook file was provided.");
    }
    FilePart<File> filePart = multiPartBody.getFile("hookFile");
    File hookFile = filePart.getFile();
    String hookText = getHookTextFromFile(hookFile);

    Hook hook =
        Hook.create(
            customerUUID,
            form.getName(),
            form.getExecutionLang(),
            hookText,
            form.isUseSudo(),
            form.getRuntimeArgs());

    form.setHookText(hookText);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Hook,
            hook.uuid.toString(),
            Audit.ActionType.CreateHook,
            Json.toJson(form),
            null);
    log.info("Created hook {} with UUID {}", hook.name, hook.uuid);
    return PlatformResults.withData(hook);
  }

  @ApiOperation(value = "Delete a hook", nickname = "deleteHook", response = YBPSuccess.class)
  public Result delete(UUID customerUUID, UUID hookUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer);
    Hook hook = Hook.getOrBadRequest(customerUUID, hookUUID);
    log.info("Deleting hook {} with UUID {}", hook.name, hookUUID);
    hook.delete();
    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.Hook, hookUUID.toString(), Audit.ActionType.DeleteHook);
    return YBPSuccess.empty();
  }

  @ApiOperation(value = "Update a hook", nickname = "updateHook", response = Hook.class)
  public Result update(UUID customerUUID, UUID hookUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer);
    Form<HookRequestData> formData = formFactory.getFormDataOrBadRequest(HookRequestData.class);
    HookRequestData form = formData.get();
    boolean isSudoEnabled = rConfigFactory.globalRuntimeConf().getBoolean(ENABLE_SUDO_PATH);

    MultipartFormData<File> multiPartBody = request().body().asMultipartFormData();
    if (multiPartBody == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No custom hook file was provided.");
    }
    FilePart<File> filePart = multiPartBody.getFile("hookFile");
    File hookFile = filePart.getFile();
    String hookText = getHookTextFromFile(hookFile);

    Hook hook = Hook.getOrBadRequest(customerUUID, hookUUID);
    boolean isNameChanged = !hook.name.equals(form.getName());
    form.verify(customerUUID, isNameChanged, isSudoEnabled);

    log.info("Updating hook {} with UUID {}", hook.name, hook.uuid);
    hook.name = form.getName();
    hook.executionLang = form.getExecutionLang();
    hook.hookText = hookText;
    hook.useSudo = form.isUseSudo();
    hook.runtimeArgs = form.getRuntimeArgs();
    hook.update();

    form.setHookText(hookText);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Hook,
            hook.uuid.toString(),
            Audit.ActionType.UpdateHook,
            Json.toJson(form),
            null);
    return PlatformResults.withData(hook);
  }

  @ApiOperation(value = "Run API Triggered hooks", nickname = "runHooks", response = YBPTask.class)
  public Result run(UUID customerUUID, UUID universeUUID, Boolean isRolling, UUID clusterUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer);
    if (!rConfigFactory.globalRuntimeConf().getBoolean(ENABLE_API_HOOK_RUN_PATH)) {
      throw new PlatformServiceException(
          UNAUTHORIZED,
          "The execution of API Triggered custom hooks is not enabled on this Anywhere instance");
    }
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    RunApiTriggeredHooks.Params taskParams = new RunApiTriggeredHooks.Params();
    taskParams.universeUUID = universe.universeUUID;
    taskParams.creatingUser = CommonUtils.getUserFromContext(ctx());
    taskParams.isRolling = isRolling.booleanValue();

    log.info(
        "Running API Triggered hooks for {} [ {} ] customer {}, cluster {}.",
        universe.name,
        universe.universeUUID,
        customer.uuid,
        clusterUUID);

    CustomerTask.TargetType target = CustomerTask.TargetType.Universe;
    if (clusterUUID != null) {
      target = CustomerTask.TargetType.Cluster;
      taskParams.clusterUUID = clusterUUID;
    }

    UUID taskUUID = commissioner.submit(TaskType.RunApiTriggeredHooks, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        target,
        CustomerTask.TaskType.RunApiTriggeredHooks,
        universe.name);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe, // TODO: do we need this to be cluster as well? There is no
            // Audit.TargetType.Cluster
            universe.universeUUID.toString(),
            Audit.ActionType.RunApiTriggeredHooks,
            taskUUID);

    return new YBPTask(taskUUID).asResult();
  }

  public String getHookTextFromFile(File hookFile) {
    try (FileInputStream fis = new FileInputStream(hookFile);
        BufferedInputStream bis = new BufferedInputStream(fis)) {
      return IOUtils.toString(bis, "UTF-8");
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "File reading failed with exception: " + e.getMessage());
    }
  }

  public void verifyAuth(Customer customer) {
    if (!rConfigFactory.globalRuntimeConf().getBoolean(ENABLE_CUSTOM_HOOKS_PATH))
      throw new PlatformServiceException(
          UNAUTHORIZED, "Custom hooks is not enabled on this Anywhere instance");
    boolean cloudEnabled = rConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled");
    if (cloudEnabled) {
      log.warn(
          "Not performing SuperAdmin authorization for this endpoint, customer={} as platform is in"
              + " cloud mode",
          customer.uuid);
      tokenAuthenticator.adminOrThrow(ctx());
    } else {
      tokenAuthenticator.superAdminOrThrow(ctx());
    }
  }
}
