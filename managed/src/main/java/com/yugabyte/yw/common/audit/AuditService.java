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
import com.google.common.collect.ImmutableList;
import com.jayway.jsonpath.Configuration;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.spi.json.JacksonJsonNodeJsonProvider;
import com.jayway.jsonpath.spi.mapper.JacksonMappingProvider;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Singleton;

import play.libs.Json;
import play.mvc.Http;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;

@Singleton
public class AuditService {

  public static final Logger LOG = LoggerFactory.getLogger(AuditService.class);

  public static final String SECRET_REPLACEMENT = "REDACTED";
  // List of json paths to any secret fields we want to redact in audit entries.
  // More on json path format can be found here: https://goessner.net/articles/JsonPath/
  public static final List<String> SECRET_PATHS =
      ImmutableList.of(
          "$..password",
          "$..confirmPassword",
          "$..ysqlPassword",
          "$..ycqlPassword",
          "$..ysqlAdminPassword",
          "$..ycqlAdminPassword",
          "$..ysqlCurrAdminPassword",
          "$..ycqlCurrAdminPassword",
          "$..['config.AWS_ACCESS_KEY_ID']",
          "$..['config.AWS_SECRET_ACCESS_KEY']",
          // GCP private key
          "$..['config.config_file_contents.private_key_id']",
          "$..['config.config_file_contents.private_key']",
          "$..config.private_key_id",
          "$..config.private_key",
          "$..GCP_CONFIG.private_key_id",
          "$..GCP_CONFIG.private_key",
          // Azure client secret
          "$..['config.AZURE_CLIENT_SECRET']",
          // Kubernetes secrets
          "$..KUBECONFIG_PULL_SECRET_CONTENT",
          "$..KUBECONFIG_CONTENT",
          // onprem and certificate private keys
          "$..keyContent",
          "$..['customServerCertData.serverKeyContent']",
          // S3 storage credentials
          "$..AWS_ACCESS_KEY_ID",
          "$..AWS_SECRET_ACCESS_KEY",
          // GCS storage credentials
          "$..GCS_CREDENTIALS_JSON",
          // Azure storage credentials
          "$..AZURE_STORAGE_SAS_TOKEN",
          // HA cluster credentials
          "$..cluster_key",
          // SmartKey API key
          "$..api_key",
          // SMTP password
          "$..smtpPassword",
          // Hashicorp token
          "$..HC_VAULT_TOKEN",
          "$..vaultToken");

  public static final List<JsonPath> SECRET_JSON_PATHS =
      SECRET_PATHS.stream().map(JsonPath::compile).collect(Collectors.toList());

  private static final Configuration JSONPATH_CONFIG =
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

  // TODO make this internal method and use createAuditEntryWithReqBody
  @Deprecated
  public void createAuditEntry(
      Http.Context ctx,
      Http.Request request,
      Audit.TargetType target,
      String targetID,
      Audit.ActionType action,
      JsonNode params,
      UUID taskUUID) {
    UserWithFeatures user = (UserWithFeatures) ctx.args.get("user");
    ctx.args.put("isAudited", true);
    String method = request.method();
    String path = request.path();
    JsonNode redactedParams = filterSecretFields(params);
    Audit entry =
        Audit.create(
            user.getUser(), path, method, target, targetID, action, redactedParams, taskUUID);
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

  public static JsonNode filterSecretFields(JsonNode input) {
    if (input == null) {
      return null;
    }
    DocumentContext context = JsonPath.parse(input.deepCopy(), JSONPATH_CONFIG);

    SECRET_JSON_PATHS.forEach(path -> context.set(path, SECRET_REPLACEMENT));

    return context.json();
  }
}
