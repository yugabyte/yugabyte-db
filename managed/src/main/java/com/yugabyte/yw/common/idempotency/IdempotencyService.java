// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.idempotency;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.UNPROCESSABLE_ENTITY;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.ApiIdempotencyKey;
import com.yugabyte.yw.models.ApiIdempotencyKey.State;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import io.ebean.DB;
import io.ebean.DuplicateKeyException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Duration;
import java.util.Date;
import java.util.HexFormat;
import java.util.Optional;
import java.util.UUID;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.util.ByteString;
import play.http.HttpEntity;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

/**
 * Makes a mutating API operation safe to retry with an {@code Idempotency-Key} request header
 * (draft-ietf-httpapi-idempotency-key-header).
 *
 * <p>The first request with a key claims it for the caller's customer and runs the operation. A
 * successful (2xx) response is stored, and a retry with the same key and the same request gets that
 * response back, with an {@code Idempotent-Replayed: true} header, without running the operation
 * again. So a client that lost the response to a create, e.g. on a timeout, retries and learns the
 * task and resource the first attempt started instead of starting a second one.
 *
 * <ul>
 *   <li>A retry while the first request still runs gets 409.
 *   <li>The same key with a different method, path or body gets 422.
 *   <li>A failed request (non-2xx or exception) releases the key, so the client can retry it.
 *   <li>A key is kept for {@link GlobalConfKeys#idempotencyKeyRetention}, after which it can be
 *       reused.
 * </ul>
 *
 * <p>Keys are scoped to the authenticated customer, so this must run after authentication: the
 * generated v2 controllers call it from the operation body, which runs after both the RBAC and the
 * legacy token authentication actions.
 */
@Slf4j
@Singleton
public class IdempotencyService {

  public static final String IDEMPOTENCY_KEY_HEADER = "Idempotency-Key";
  public static final String REPLAYED_HEADER = "Idempotent-Replayed";

  static final int MAX_KEY_LENGTH = 255;
  // Visible ASCII only: the key is an opaque client-generated token, typically a UUID.
  private static final Pattern KEY_PATTERN = Pattern.compile("[\\x21-\\x7E]+");
  private static final Duration CLEANUP_INTERVAL = Duration.ofHours(1);

  // Sorts object keys so that the same JSON body with differently ordered fields fingerprints the
  // same.
  private static final ObjectMapper CANONICAL_MAPPER =
      Json.mapper().copy().configure(SerializationFeature.ORDER_MAP_ENTRIES_BY_KEYS, true);

  @FunctionalInterface
  public interface ResultSupplier {
    Result get() throws Exception;
  }

  private final RuntimeConfGetter confGetter;
  private final PlatformScheduler platformScheduler;

  @Inject
  public IdempotencyService(RuntimeConfGetter confGetter, PlatformScheduler platformScheduler) {
    this.confGetter = confGetter;
    this.platformScheduler = platformScheduler;
  }

  /** Schedules the periodic removal of expired keys. */
  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(), Duration.ZERO, CLEANUP_INTERVAL, this::deleteExpiredKeys);
  }

  @VisibleForTesting
  public void deleteExpiredKeys() {
    try {
      int deleted = ApiIdempotencyKey.deleteExpired(new Date());
      if (deleted > 0) {
        log.info("Deleted {} expired idempotency keys", deleted);
      }
    } catch (Exception e) {
      log.error("Failed to delete expired idempotency keys", e);
    }
  }

  /**
   * Runs {@code operation} unless the request carries an {@code Idempotency-Key} that was already
   * used, in which case it returns the stored response of the earlier request (or fails, see the
   * class description). Without the header the operation simply runs.
   */
  public Result execute(Http.Request request, ResultSupplier operation) throws Exception {
    Optional<String> header = request.header(IDEMPOTENCY_KEY_HEADER);
    if (header.isEmpty()) {
      return operation.get();
    }
    String key = validateKey(header.get());
    Customer customer = RequestContext.getIfPresent(TokenAuthenticator.CUSTOMER);
    if (customer == null) {
      // Only reachable if an unauthenticated operation is marked idempotent: keys are per customer.
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, IDEMPOTENCY_KEY_HEADER + " needs an authenticated customer");
    }
    UserWithFeatures user = RequestContext.getIfPresent(TokenAuthenticator.USER);
    UUID userUUID = user == null ? null : user.getUser().getUuid();

    Claim claim = claim(customer.getUuid(), key, userUUID, request, fingerprint(request));
    if (claim.replay != null) {
      log.info(
          "Replaying the response stored for {} {} under idempotency key {}",
          request.method(),
          request.path(),
          key);
      return claim.replay;
    }

    Result result;
    try {
      result = operation.get();
    } catch (Throwable t) {
      release(claim.record);
      throw t;
    }
    if (isSuccess(result.status())) {
      complete(claim.record, result);
    } else {
      release(claim.record);
    }
    return result;
  }

  private static String validateKey(String key) {
    if (key.isEmpty() || key.length() > MAX_KEY_LENGTH || !KEY_PATTERN.matcher(key).matches()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          IDEMPOTENCY_KEY_HEADER
              + " must be 1 to "
              + MAX_KEY_LENGTH
              + " visible ASCII characters, e.g. a UUID");
    }
    return key;
  }

  /** Either a claimed key to run the operation under, or a stored response to replay. */
  private static class Claim {
    final ApiIdempotencyKey record;
    final Result replay;

    private Claim(ApiIdempotencyKey record, Result replay) {
      this.record = record;
      this.replay = replay;
    }
  }

  private Claim claim(
      UUID customerUUID, String key, UUID userUUID, Http.Request request, String fingerprint) {
    // Two attempts: the second one covers a row that expired or was released between our insert
    // failing and our lookup.
    for (int attempt = 0; attempt < 2; attempt++) {
      Date now = new Date();
      ApiIdempotencyKey record = new ApiIdempotencyKey();
      record.setUuid(UUID.randomUUID());
      record.setCustomerUUID(customerUUID);
      record.setIdempotencyKey(key);
      record.setUserUUID(userUUID);
      record.setRequestMethod(request.method());
      record.setRequestPath(request.path());
      record.setRequestFingerprint(fingerprint);
      record.setState(State.IN_PROGRESS);
      record.setCreateTime(now);
      record.setUpdateTime(now);
      Duration retention = confGetter.getGlobalConf(GlobalConfKeys.idempotencyKeyRetention);
      record.setExpireTime(plus(now, retention));
      try {
        record.insert();
        return new Claim(record, null);
      } catch (DuplicateKeyException e) {
        // Used before: decide below.
      }

      Optional<ApiIdempotencyKey> existing = ApiIdempotencyKey.find(customerUUID, key);
      if (existing.isEmpty()) {
        continue;
      }
      ApiIdempotencyKey used = existing.get();
      if (used.getExpireTime().before(now)) {
        deleteIfUnchanged(used);
        continue;
      }
      if (!used.getRequestFingerprint().equals(fingerprint)) {
        throw new PlatformServiceException(
            UNPROCESSABLE_ENTITY,
            IDEMPOTENCY_KEY_HEADER
                + " "
                + key
                + " was already used for a different request: "
                + used.getRequestMethod()
                + " "
                + used.getRequestPath());
      }
      if (used.getState() == State.COMPLETED) {
        return new Claim(used, replay(used));
      }
      Duration inProgressTimeout =
          confGetter.getGlobalConf(GlobalConfKeys.idempotencyKeyInProgressTimeout);
      if (plus(used.getUpdateTime(), inProgressTimeout).before(now) && takeOver(used, now)) {
        log.warn(
            "Taking over idempotency key {}: its request started at {} and never finished",
            key,
            used.getUpdateTime());
        return new Claim(used, null);
      }
      throw new PlatformServiceException(
          CONFLICT,
          "A request with " + IDEMPOTENCY_KEY_HEADER + " " + key + " is still in progress");
    }
    throw new PlatformServiceException(
        CONFLICT, "A concurrent request is using " + IDEMPOTENCY_KEY_HEADER + " " + key);
  }

  /** Claims an abandoned in-progress key, unless another request claimed it first. */
  private static boolean takeOver(ApiIdempotencyKey used, Date now) {
    int updated =
        DB.sqlUpdate(
                "UPDATE api_idempotency_key SET update_time = :now"
                    + " WHERE uuid = :uuid AND state = :state AND update_time = :updateTime")
            .setParameter("now", now)
            .setParameter("uuid", used.getUuid())
            .setParameter("state", State.IN_PROGRESS.name())
            .setParameter("updateTime", used.getUpdateTime())
            .execute();
    if (updated == 1) {
      used.setUpdateTime(now);
      return true;
    }
    return false;
  }

  private static void deleteIfUnchanged(ApiIdempotencyKey used) {
    DB.sqlUpdate("DELETE FROM api_idempotency_key WHERE uuid = :uuid AND update_time = :updateTime")
        .setParameter("uuid", used.getUuid())
        .setParameter("updateTime", used.getUpdateTime())
        .execute();
  }

  private static void complete(ApiIdempotencyKey record, Result result) {
    try {
      record.setResponseStatus(result.status());
      record.setResponseContentType(result.body().contentType().orElse(null));
      record.setResponseBody(bodyAsString(result));
      record.setState(State.COMPLETED);
      record.setUpdateTime(new Date());
      record.update();
    } catch (Exception e) {
      // The operation succeeded; failing the request now would make the client retry it and run
      // it twice. Release the key instead: a retry then runs again, as it would without a key.
      log.error(
          "Failed to store the response for idempotency key {}, releasing it",
          record.getIdempotencyKey(),
          e);
      release(record);
    }
  }

  private static void release(ApiIdempotencyKey record) {
    try {
      deleteIfUnchanged(record);
    } catch (Exception e) {
      log.error("Failed to release idempotency key {}", record.getIdempotencyKey(), e);
    }
  }

  private static Result replay(ApiIdempotencyKey used) {
    Result result = Results.status(used.getResponseStatus());
    if (used.getResponseBody() != null) {
      result =
          used.getResponseContentType() != null
              ? Results.status(used.getResponseStatus(), used.getResponseBody())
                  .as(used.getResponseContentType())
              : Results.status(used.getResponseStatus(), used.getResponseBody());
    }
    return result.withHeader(REPLAYED_HEADER, "true");
  }

  private static String bodyAsString(Result result) {
    HttpEntity body = result.body();
    if (body instanceof HttpEntity.Strict) {
      return ((HttpEntity.Strict) body).data().utf8String();
    }
    if (body.isKnownEmpty()) {
      return null;
    }
    // Generated v2 operations return strict JSON bodies; a streamed body can't be stored.
    throw new IllegalStateException("Cannot store a streamed response body for replay");
  }

  static boolean isSuccess(int status) {
    return status >= 200 && status < 300;
  }

  /**
   * SHA-256 over the method, path and query, and the body: a JSON body is canonicalised (object
   * keys sorted) so that a client re-serialising the same request gets the same fingerprint.
   */
  @VisibleForTesting
  public static String fingerprint(Http.Request request) {
    try {
      MessageDigest digest = MessageDigest.getInstance("SHA-256");
      digest.update(request.method().getBytes(StandardCharsets.UTF_8));
      digest.update((byte) ' ');
      digest.update(request.uri().getBytes(StandardCharsets.UTF_8));
      digest.update((byte) '\n');
      JsonNode json = request.body().asJson();
      if (json != null) {
        Object canonical = CANONICAL_MAPPER.treeToValue(json, Object.class);
        digest.update(CANONICAL_MAPPER.writeValueAsBytes(canonical));
      } else {
        ByteString bytes = request.body().asBytes();
        if (bytes != null) {
          digest.update(bytes.toArray());
        }
      }
      return HexFormat.of().formatHex(digest.digest());
    } catch (NoSuchAlgorithmException | java.io.IOException e) {
      throw new IllegalStateException("Cannot fingerprint the request", e);
    }
  }

  private static Date plus(Date date, Duration duration) {
    return new Date(date.getTime() + duration.toMillis());
  }
}
