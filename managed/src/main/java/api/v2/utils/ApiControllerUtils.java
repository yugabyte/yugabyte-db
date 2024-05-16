/*
 * Copyright 2024 YugaByte, Inc. and Contributors
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
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.audit.AuditService;
import java.util.concurrent.ExecutionException;
import java.util.function.Function;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.stream.Materializer;
import org.apache.pekko.util.ByteString;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Http.Request;
import play.mvc.Http.RequestBody;
import play.mvc.Result;

@Slf4j
public class ApiControllerUtils {
  @Inject private AuditService auditService;
  @Inject private Materializer mat;
  protected ObjectMapper mapper =
      Json.mapper()
          .copy()
          .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
          .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);

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

  protected Request replaceRequestBody(Request request, Object data) {
    return request.withBody(new RequestBody(Json.toJson(data)));
  }

  protected <T> T extractFromResult(Result result, Class<T> resultType)
      throws InterruptedException, JsonProcessingException, ExecutionException {
    ByteString resultBytes =
        result.body().consumeData(mat).thenApply(Function.identity()).toCompletableFuture().get();
    String resultString = resultBytes.decodeString(result.charset().orElse("utf-8"));
    T resultObj = mapper.readValue(resultString, resultType);
    return resultObj;
  }

  /*
   * Convert the given source bean into targetClassType and add to request body. The first step assumes that the source is mappable to the given target type. Throws exception otherwise.
   */
  protected <S, T> Request convertToRequest(
      S source,
      Class<T> targetClassType,
      JsonDeserializer<? extends T> targetDeser,
      Request request) {
    return replaceRequestBody(request, convert(source, targetClassType, targetDeser));
  }

  /*
   * Extracts the body from given result with the assumption that it is of type sourceType, and bean maps that to the targetType.
   */
  protected <S, T> T convertResult(
      Result result,
      Class<S> sourceType,
      Class<T> targetType,
      JsonDeserializer<? extends T> targetDeser)
      throws Exception {
    S source = extractFromResult(result, sourceType);
    return convert(source, targetType, targetDeser);
  }

  protected AuditService auditService() {
    return auditService;
  }
}
