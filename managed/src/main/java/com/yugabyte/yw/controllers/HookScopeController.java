// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.HookScopeFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.data.Form;
import play.mvc.Http;
import play.mvc.Result;

@Slf4j
@Api(
    value = "Hook Scope Management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH),
    hidden = true)
public class HookScopeController extends AuthenticatedController {

  public static final String ENABLE_CUSTOM_HOOKS_PATH =
      "yb.security.custom_hooks.enable_custom_hooks";

  @Inject private TokenAuthenticator tokenAuthenticator;

  @Inject RuntimeConfigFactory rConfigFactory;

  @Inject RuntimeConfGetter confGetter;

  @ApiOperation(
      value = "List scopes",
      nickname = "listScopes",
      response = HookScope.class,
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
    List<HookScope> hookScopes = HookScope.getAll(customerUUID);
    return PlatformResults.withData(hookScopes);
  }

  @ApiOperation(
      value = "Create a hook scope",
      nickname = "createHookScope",
      response = HookScope.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "hookScope",
          value = "Hook Scope form data for new hook to be created",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.HookScopeFormData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result create(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer, request);
    Form<HookScopeFormData> formData =
        formFactory.getFormDataOrBadRequest(request, HookScopeFormData.class);
    HookScopeFormData form = formData.get();
    form.verify(customerUUID);
    HookScope hookScope;
    if (form.getUniverseUUID() != null) { // Universe Scope
      Universe universe = Universe.getOrBadRequest(form.getUniverseUUID(), customer);

      // We can create hook_scopes for clusters that do not exist yet.
      // This is an explicit requirement for the CDC / AddOn cluster work where we want to have
      // the hooks run as part of cluster creation.
      hookScope =
          HookScope.create(customerUUID, form.getTriggerType(), universe, form.getClusterUUID());
    } else if (form.getProviderUUID() != null) { // Provider Scope
      Provider provider = Provider.getOrBadRequest(customerUUID, form.getProviderUUID());
      hookScope = HookScope.create(customerUUID, form.getTriggerType(), provider);
    } else { // Global Scope
      hookScope = HookScope.create(customerUUID, form.getTriggerType());
    }
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.HookScope,
            hookScope.getUuid().toString(),
            Audit.ActionType.CreateHookScope);
    log.info("Created hook scope with uuid {}", hookScope.getUuid());
    return PlatformResults.withData(hookScope);
  }

  @ApiOperation(
      value = "Delete a hook scope",
      nickname = "deleteHookScope",
      response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(UUID customerUUID, UUID hookScopeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer, request);
    HookScope hookScope = HookScope.getOrBadRequest(customerUUID, hookScopeUUID);
    log.info("Deleting hook scope with UUID {}", hookScopeUUID);
    hookScope.delete();
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.HookScope,
            hookScopeUUID.toString(),
            Audit.ActionType.DeleteHookScope);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "Add a hook to a hook scope",
      nickname = "addHook",
      response = HookScope.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result addHook(
      UUID customerUUID, UUID hookScopeUUID, UUID hookUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer, request);
    HookScope hookScope = HookScope.getOrBadRequest(customerUUID, hookScopeUUID);
    Hook hook = Hook.getOrBadRequest(customerUUID, hookUUID);
    hookScope.addHook(hook);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.HookScope,
            hookScopeUUID.toString(),
            Audit.ActionType.AddHook);
    return PlatformResults.withData(hookScope);
  }

  @ApiOperation(
      value = "Remove a hook from a hook scope",
      nickname = "removeHook",
      response = HookScope.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result removeHook(
      UUID customerUUID, UUID hookScopeUUID, UUID hookUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    verifyAuth(customer, request);
    HookScope hookScope = HookScope.getOrBadRequest(customerUUID, hookScopeUUID);
    Hook hook = Hook.getOrBadRequest(customerUUID, hookUUID);
    if (hook.getHookScope() == null || !hook.getHookScope().getUuid().equals(hookScopeUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Hook " + hookUUID + " is not attached to hook scope " + hookScopeUUID);
    }
    hook.setHookScope(null);
    hook.update();
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.HookScope,
            hookScopeUUID.toString(),
            Audit.ActionType.RemoveHook);
    return PlatformResults.withData(hookScope);
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
