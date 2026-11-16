/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package api.v2.utils;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonDeserializer;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.idempotency.IdempotencyService;
import com.yugabyte.yw.models.helpers.CommonUtils;
import jakarta.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Slf4j
public class ApiControllerUtils {
  private final AuditService auditService;

  // Field-injected so that the generated *ImpInterface constructors, and every controller
  // implementation calling them, stay unchanged.
  @Inject private IdempotencyService idempotencyService;

  @Inject
  public ApiControllerUtils(AuditService auditService) {
    this.auditService = auditService;
  }

  /**
   * Runs an operation marked {@code x-yba-api-idempotent} in the spec, honouring the request's
   * {@code Idempotency-Key} header. Called by the generated code.
   */
  protected Result idempotent(Http.Request request, IdempotencyService.ResultSupplier operation)
      throws Exception {
    if (idempotencyService == null) {
      // A controller implementation built by hand in a unit test, outside Guice.
      return operation.get();
    }
    return idempotencyService.execute(request, operation);
  }

  protected ObjectMapper mapper =
      Json.mapper()
          .copy()
          .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
          .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false)
          .configure(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS, false);

  protected <T> void registerDeserializer(Class<T> type, JsonDeserializer<? extends T> deser) {
    SimpleModule module = new SimpleModule();
    module.addDeserializer(type, deser);
    mapper.registerModule(module);
  }

  /*
   * Convenience method to convert a given bean to a target type. Assumes the two classes are bean mappable.
   */
  protected <S, T> T convert(
      S source, Class<T> targetClassType, JsonDeserializer<? extends T> deser) {
    T target;
    try {
      if (deser != null) {
        registerDeserializer(targetClassType, deser);
      }
      target = mapper.readValue(mapper.writeValueAsString(source), targetClassType);
    } catch (Exception e) {
      log.error("Failed at creating target type", e);
      throw new PlatformServiceException(Http.Status.BAD_REQUEST, "Failed at creating target type");
    }
    return target;
  }

  protected AuditService auditService() {
    return auditService;
  }

  /** Pretty print a json serializable object for logging, with secrets redacted. */
  protected String prettyPrint(Object obj) {
    try {
      ObjectNode objNode = CommonUtils.maskConfig((ObjectNode) Json.toJson(obj));
      JsonNode redacted = RedactingService.filterSecretFields(objNode, RedactionTarget.LOGS);
      return mapper.writerWithDefaultPrettyPrinter().writeValueAsString(redacted);
    } catch (JsonProcessingException e) {
      log.error("Failed at pretty printing object", e);
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Failed at pretty printing object");
    }
  }
}
