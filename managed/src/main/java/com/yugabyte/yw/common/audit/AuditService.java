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
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.spi.json.JacksonJsonNodeJsonProvider;
import com.jayway.jsonpath.spi.mapper.JacksonMappingProvider;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Users;
import io.ebean.Model;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

@Singleton
public class AuditService {

  private static final Logger LOG = LoggerFactory.getLogger(AuditService.class);

  public static final String SECRET_REPLACEMENT = "Redacted";
  private static final String SECRET_PARAM_PATHS = "yb.audit.secret_param_paths";
  private static final String MIGRATED_SECRET_PARAM_PATHS = "yb.audit.migraded_secret_param_paths";
  private final Configuration configuration;
  private final List<JsonPath> secretParamPaths;
  private final SettableRuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public AuditService(Config config,
                      SettableRuntimeConfigFactory configFactory) {
    configuration = Configuration.builder()
      .jsonProvider(new JacksonJsonNodeJsonProvider())
      .mappingProvider(new JacksonMappingProvider())
      .build();

    List<String> secretParamPathsList = config.getStringList(SECRET_PARAM_PATHS);
    secretParamPaths = secretParamPathsList.stream()
      .map(JsonPath::compile)
      .collect(Collectors.toList());

    runtimeConfigFactory = configFactory;
  }

  public void createAuditEntry(Http.Context ctx, Http.Request request) {
    createAuditEntry(ctx, request, null, null);
  }

  /**
   * Writes audit entry along with request details. This redacts all the secret fields, defined
   * in yb.audit.secret_param_paths property. If you're using this method to write audit - make sure
   * all the secret fields are covered by the above property.
   * @param ctx request context
   * @param request request
   * @param params request body
   */
  public void createAuditEntry(Http.Context ctx, Http.Request request, JsonNode params) {
    createAuditEntry(ctx, request, params, null);
  }

  public void createAuditEntry(Http.Context ctx, Http.Request request, UUID taskUUID) {
    createAuditEntry(ctx, request, null, taskUUID);
  }

  public void createAuditEntry(
    Http.Context ctx, Http.Request request, JsonNode params, UUID taskUUID) {
    Users user = (Users) ctx.args.get("user");
    String method = request.method();
    String path = request.path();
    JsonNode redactedParams = filterSecretFields(params, secretParamPaths);
    Audit.create(user.uuid, user.customerUUID, path, method, redactedParams, taskUUID);
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

  public void redactSecretsFromAuditMigration() {
    RuntimeConfig<Model> runtimeConfig = runtimeConfigFactory.globalRuntimeConf();
    String migratedPathsStr = runtimeConfig.getStringOrElse(MIGRATED_SECRET_PARAM_PATHS, "");

    Set<JsonPath> migratedPaths = Arrays.stream(migratedPathsStr.split(","))
      .filter(StringUtils::isNotEmpty)
      .map(JsonPath::compile)
      .collect(Collectors.toSet());

    List<JsonPath> notMigratedPaths = secretParamPaths.stream()
      .filter(path -> !migratedPaths.contains(path))
      .collect(Collectors.toList());

    if (notMigratedPaths.isEmpty()) {
      return;
    }

    Audit.forEachEntry(auditEntry -> {
      JsonNode payload = auditEntry.getPayload();
      if (payload == null) {
        return;
      }
      JsonNode newPayload = filterSecretFields(payload, notMigratedPaths);
      if (newPayload.equals(payload)) {
        return;
      }
      auditEntry.setPayload(newPayload);
      auditEntry.save();
      LOG.debug("Redacted audit entry {}", auditEntry.getAuditID());
    });

    String newMigratedPaths = secretParamPaths.stream()
      .map(JsonPath::getPath)
      .collect(Collectors.joining(","));
    runtimeConfig.setValue(MIGRATED_SECRET_PARAM_PATHS, newMigratedPaths);
  }

  private JsonNode filterSecretFields(JsonNode input, List<JsonPath> secretPaths) {
    if (input == null) {
      return null;
    }
    DocumentContext context = JsonPath.parse(input.deepCopy(), configuration);

    secretPaths.forEach(path -> context.set(path, SECRET_REPLACEMENT));

    return context.json();
  }
}
