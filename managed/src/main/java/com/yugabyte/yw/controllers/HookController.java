// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.RunApiTriggeredHooks;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.HookRequestData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.BufferedInputStream;
import java.io.FileInputStream;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.IOUtils;
import play.data.Form;
import play.libs.Files.TemporaryFile;
import play.libs.Json;
import play.mvc.Http;
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
  @Inject RuntimeConfGetter confGetter;

  @ApiOperation(
      value = "List all hooks",
      nickname = "listHooks",
      response = Hook.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer, request);
    List<Hook> hooks = Hook.getAll(customerUUID);
    return PlatformResults.withData(hooks);
  }

  @ApiOperation(value = "Create a Hook", nickname = "createHook", response = Hook.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result create(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer, request);
    Form<HookRequestData> formData =
        formFactory.getFormDataOrBadRequest(request, HookRequestData.class);
    HookRequestData form = formData.get();
    boolean isSudoEnabled = confGetter.getGlobalConf(GlobalConfKeys.enableSudo);
    form.verify(customerUUID, true, isSudoEnabled);

    MultipartFormData<TemporaryFile> multiPartBody = request.body().asMultipartFormData();
    if (multiPartBody == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No custom hook file was provided.");
    }
    FilePart<TemporaryFile> filePart = multiPartBody.getFile("hookFile");
    TemporaryFile hookFile = filePart.getRef();
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
            request,
            Audit.TargetType.Hook,
            hook.getUuid().toString(),
            Audit.ActionType.CreateHook,
            Json.toJson(form));
    log.info("Created hook {} with UUID {}", hook.getName(), hook.getUuid());
    return PlatformResults.withData(hook);
  }

  @ApiOperation(value = "Delete a hook", nickname = "deleteHook", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(UUID customerUUID, UUID hookUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer, request);
    Hook hook = Hook.getOrBadRequest(customerUUID, hookUUID);
    log.info("Deleting hook {} with UUID {}", hook.getName(), hookUUID);
    hook.delete();
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Hook, hookUUID.toString(), Audit.ActionType.DeleteHook);
    return YBPSuccess.empty();
  }

  @ApiOperation(value = "Update a hook", nickname = "updateHook", response = Hook.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result update(UUID customerUUID, UUID hookUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer, request);
    Form<HookRequestData> formData =
        formFactory.getFormDataOrBadRequest(request, HookRequestData.class);
    HookRequestData form = formData.get();
    boolean isSudoEnabled = confGetter.getGlobalConf(GlobalConfKeys.enableSudo);

    MultipartFormData<TemporaryFile> multiPartBody = request.body().asMultipartFormData();
    if (multiPartBody == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No custom hook file was provided.");
    }
    FilePart<TemporaryFile> filePart = multiPartBody.getFile("hookFile");
    TemporaryFile hookFile = filePart.getRef();
    String hookText = getHookTextFromFile(hookFile);

    Hook hook = Hook.getOrBadRequest(customerUUID, hookUUID);
    boolean isNameChanged = !hook.getName().equals(form.getName());
    form.verify(customerUUID, isNameChanged, isSudoEnabled);

    log.info("Updating hook {} with UUID {}", hook.getName(), hook.getUuid());
    hook.setName(form.getName());
    hook.setExecutionLang(form.getExecutionLang());
    hook.setHookText(hookText);
    hook.setUseSudo(form.isUseSudo());
    hook.setRuntimeArgs(form.getRuntimeArgs());
    hook.update();

    form.setHookText(hookText);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Hook,
            hook.getUuid().toString(),
            Audit.ActionType.UpdateHook,
            Json.toJson(form));
    return PlatformResults.withData(hook);
  }

  @ApiOperation(value = "Run API Triggered hooks", nickname = "runHooks", response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result run(
      UUID customerUUID,
      UUID universeUUID,
      Boolean isRolling,
      UUID clusterUUID,
      List<UUID> hookUUIDs,
      Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer, request);
    if (!confGetter.getGlobalConf(GlobalConfKeys.enabledApiTriggerHooks)) {
      throw new PlatformServiceException(
          UNAUTHORIZED,
          "The execution of API Triggered custom hooks is not enabled on this Anywhere instance");
    }
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (clusterUUID != null) {
      if (universe.getCluster(clusterUUID) == null) {
        throw new PlatformServiceException(BAD_REQUEST, "Cannot find cluster: " + clusterUUID);
      }
    }

    // Validate the hookUUIDs if any were provided.
    hookUUIDs.forEach(
        id ->
            Hook.getOrBadRequest(customerUUID, id)
                .getHookScopeOrFail(universeUUID, clusterUUID, HookScope.TriggerType.ApiTriggered));

    RunApiTriggeredHooks.Params taskParams = new RunApiTriggeredHooks.Params();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.creatingUser = CommonUtils.getUserFromContext();
    taskParams.isRolling = isRolling.booleanValue();
    taskParams.hookUUIDs = hookUUIDs;

    log.info(
        "Running API Triggered hooks for {} [ {} ] customer {}, cluster {}.",
        universe.getName(),
        universe.getUniverseUUID(),
        customer.getUuid(),
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
        universe.getName());
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe, // TODO: do we need this to be cluster as well? There is no
            // Audit.TargetType.Cluster
            universe.getUniverseUUID().toString(),
            Audit.ActionType.RunApiTriggeredHooks,
            taskUUID);

    return new YBPTask(taskUUID).asResult();
  }

  public String getHookTextFromFile(TemporaryFile hookFile) {
    try (FileInputStream fis = new FileInputStream(hookFile.path().toFile());
        BufferedInputStream bis = new BufferedInputStream(fis)) {
      return IOUtils.toString(bis, "UTF-8");
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "File reading failed with exception: " + e.getMessage());
    }
  }

  public void verifyAuth(Customer customer, Http.Request request) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableCustomHooks))
      throw new PlatformServiceException(
          UNAUTHORIZED, "Custom hooks is not enabled on this Anywhere instance");
    boolean cloudEnabled = rConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled");
    if (cloudEnabled) {
      log.warn(
          "Not performing SuperAdmin authorization for this endpoint, customer={} as platform is in"
              + " cloud mode",
          customer.getUuid());
      tokenAuthenticator.adminOrThrow(request);
    } else {
      tokenAuthenticator.superAdminOrThrow(request);
    }
  }
}
