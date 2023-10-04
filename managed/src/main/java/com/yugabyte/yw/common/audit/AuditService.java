/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.audit;

import com.fasterxml.jackson.databind.JsonNode;
import com.jayway.jsonpath.Configuration;
import com.jayway.jsonpath.spi.json.JacksonJsonNodeJsonProvider;
import com.jayway.jsonpath.spi.mapper.JacksonMappingProvider;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import java.util.List;
import java.util.UUID;
import javax.inject.Singleton;

import play.libs.Json;
import play.mvc.Http;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;

@Singleton
public class AuditService {

  public static final Logger LOG = LoggerFactory.getLogger(AuditService.class);

  public static final Configuration JSONPATH_CONFIG =
      Configuration.builder()
          .jsonProvider(new JacksonJsonNodeJsonProvider())
          .mappingProvider(new JacksonMappingProvider())
          .build();

  public void createAuditEntry(Http.Context ctx, Http.Request request) {
    createAuditEntry(ctx, request, null, null, null, null, null);
  }

  /**
   * Writes audit entry along with request details. This redacts all the secret fields, defined in
   * yb.audit.secret_param_paths property. If you're using this method to write audit - make sure
   * all the secret fields are covered by the above property.
   *
   * @param ctx request context
   * @param request request
   * @param params request body
   */
  public void createAuditEntry(Http.Context ctx, Http.Request request, JsonNode params) {
    createAuditEntry(ctx, request, null, null, null, params, null);
  }

  public void createAuditEntry(
      Http.Context ctx,
      Http.Request request,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action,
      JsonNode params) {
    createAuditEntry(ctx, request, target, targetID, action, params, null);
  }

  public void createAuditEntry(Http.Context ctx, Http.Request request, UUID taskUUID) {
    createAuditEntry(ctx, request, null, null, null, null, taskUUID);
  }

  public void createAuditEntry(
      Http.Context ctx,
      Http.Request request,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action) {
    createAuditEntry(ctx, request, target, targetID, action, null, null);
  }

  public void createAuditEntry(
      Http.Context ctx, Http.Request request, JsonNode params, UUID taskUUID) {
    createAuditEntry(ctx, request, null, null, null, params, taskUUID);
  }

  public void createAuditEntry(
      Http.Context ctx,
      Http.Request request,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action,
      UUID taskUUID) {
    createAuditEntry(ctx, request, target, targetID, action, null, taskUUID);
  }

  public void createAuditEntryWithReqBody(Http.Context ctx) {
    createAuditEntryWithReqBody(ctx, null);
  }

  public void createAuditEntryWithReqBody(Http.Context ctx, UUID taskUUID) {
    createAuditEntry(ctx, ctx.request(), null, null, null, ctx.request().body().asJson(), taskUUID);
  }

  public void createAuditEntryWithReqBody(
      Http.Context ctx, Audit.TargetType target, String targetID, Audit.ActionType action) {
    createAuditEntry(ctx, ctx.request(), target, targetID, action, null, null);
  }

  public void createAuditEntryWithReqBody(
      Http.Context ctx,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action,
      JsonNode params) {
    createAuditEntry(ctx, ctx.request(), target, targetID, action, params, null);
  }

  public void createAuditEntryWithReqBody(
      Http.Context ctx,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action,
      UUID taskUUID) {
    createAuditEntry(ctx, ctx.request(), target, targetID, action, null, taskUUID);
  }

  public void createAuditEntryWithReqBody(
      Http.Context ctx,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action,
      JsonNode params,
      UUID taskUUID) {
    createAuditEntry(ctx, ctx.request(), target, targetID, action, params, taskUUID);
  }

  public void createAuditEntryWithReqBody(
      Http.Context ctx,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action,
      JsonNode params,
      UUID taskUUID,
      JsonNode additionalDetails) {
    createAuditEntry(
        ctx, ctx.request(), target, targetID, action, params, taskUUID, additionalDetails);
  }

  public void createAuditEntry(
      Http.Context ctx,
      Http.Request request,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action,
      JsonNode params,
      UUID taskUUID) {
    createAuditEntry(ctx, request, target, targetID, action, params, taskUUID, null);
  }

  // TODO make this internal method and use createAuditEntryWithReqBody
  @Deprecated
  public void createAuditEntry(
      Http.Context ctx,
      Http.Request request,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action,
      JsonNode params,
      UUID taskUUID,
      JsonNode additionalDetails) {
    UserWithFeatures user = (UserWithFeatures) ctx.args.get("user");
    ctx.args.put("isAudited", true);
    String method = request.method();
    String path = request.path();
    JsonNode redactedParams = RedactingService.filterSecretFields(params, RedactionTarget.LOGS);
    String userAddress = request.remoteAddress();
    Audit entry =
        Audit.create(
            user.getUser(),
            path,
            method,
            target,
            targetID,
            action,
            redactedParams,
            taskUUID,
            additionalDetails);
    MDC.put("logType", "audit");
    LOG.info(Json.toJson(entry).toString());
    MDC.remove("logType");
  }

  public List<Audit> getAll(UUID customerUUID) {
    return Audit.getAll(customerUUID);
  }

  public Audit getFromTaskUUID(UUID taskUUID) {
    return Audit.getFromTaskUUID(taskUUID);
  }

  public List<Audit> getAllUserEntries(UUID userUUID) {
    return Audit.getAllUserEntries(userUUID);
  }

  public Audit getOrBadRequest(UUID customerUUID, UUID taskUUID) {
    return Audit.getOrBadRequest(customerUUID, taskUUID);
  }
}
