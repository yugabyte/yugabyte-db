// Copyright (ch Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.forms.HookScopeFormData;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Audit;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.UUID;
import java.util.List;
import play.data.Form;
import play.mvc.Result;
import lombok.extern.slf4j.Slf4j;

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

  @ApiOperation(
      value = "List scopes",
      nickname = "listScopes",
      response = HookScope.class,
      responseContainer = "List")
  public Result list(UUID customerUUID) {
    verifyAuth();
    Customer customer = Customer.getOrBadRequest(customerUUID);
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
  public Result create(UUID customerUUID) {
    verifyAuth();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Form<HookScopeFormData> formData = formFactory.getFormDataOrBadRequest(HookScopeFormData.class);
    HookScopeFormData form = formData.get();
    form.verify(customerUUID);
    HookScope hookScope;
    if (form.getUniverseUUID() != null) { // Universe Scope
      Universe universe = Universe.getValidUniverseOrBadRequest(form.getUniverseUUID(), customer);
      hookScope = HookScope.create(customerUUID, form.getTriggerType(), universe);
    } else if (form.getProviderUUID() != null) { // Provider Scope
      Provider provider = Provider.getOrBadRequest(customerUUID, form.getProviderUUID());
      hookScope = HookScope.create(customerUUID, form.getTriggerType(), provider);
    } else { // Global Scope
      hookScope = HookScope.create(customerUUID, form.getTriggerType());
    }
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.HookScope,
            hookScope.uuid.toString(),
            Audit.ActionType.CreateHookScope,
            request().body().asJson());
    log.info("Created hook scope with uuid {}", hookScope.uuid);
    return PlatformResults.withData(hookScope);
  }

  @ApiOperation(
      value = "Delete a hook scope",
      nickname = "deleteHookScope",
      response = YBPSuccess.class)
  public Result delete(UUID customerUUID, UUID hookScopeUUID) {
    verifyAuth();
    HookScope hookScope = HookScope.getOrBadRequest(customerUUID, hookScopeUUID);
    log.info("Deleting hook scope with UUID {}", hookScopeUUID);
    hookScope.delete();
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.HookScope,
            hookScopeUUID.toString(),
            Audit.ActionType.DeleteHookScope);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "Add a hook to a hook scope",
      nickname = "addHook",
      response = HookScope.class)
  public Result addHook(UUID customerUUID, UUID hookScopeUUID, UUID hookUUID) {
    verifyAuth();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    HookScope hookScope = HookScope.getOrBadRequest(customerUUID, hookScopeUUID);
    Hook hook = Hook.getOrBadRequest(customerUUID, hookUUID);
    hookScope.addHook(hook);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.HookScope, hookScopeUUID.toString(), Audit.ActionType.AddHook);
    return PlatformResults.withData(hookScope);
  }

  @ApiOperation(
      value = "Remove a hook from a hook scope",
      nickname = "removeHook",
      response = HookScope.class)
  public Result removeHook(UUID customerUUID, UUID hookScopeUUID, UUID hookUUID) {
    verifyAuth();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    HookScope hookScope = HookScope.getOrBadRequest(customerUUID, hookScopeUUID);
    Hook hook = Hook.getOrBadRequest(customerUUID, hookUUID);
    if (hook.hookScope == null || !hook.hookScope.uuid.equals(hookScopeUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Hook " + hookUUID + " is not attached to hook scope " + hookScopeUUID);
    }
    hook.hookScope = null;
    hook.update();
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.HookScope,
            hookScopeUUID.toString(),
            Audit.ActionType.RemoveHook);
    return PlatformResults.withData(hookScope);
  }

  public void verifyAuth() {
    if (!rConfigFactory.staticApplicationConf().getBoolean(ENABLE_CUSTOM_HOOKS_PATH))
      throw new PlatformServiceException(
          UNAUTHORIZED, "Custom hooks is not enabled on this Anywhere instance");
    tokenAuthenticator.superAdminOrThrow(ctx());
  }
}
