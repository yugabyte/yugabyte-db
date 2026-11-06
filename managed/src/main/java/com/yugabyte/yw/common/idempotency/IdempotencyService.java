// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.idempotency;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.UNPROCESSABLE_ENTITY;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableSet;
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
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import io.ebean.DB;
import io.ebean.DuplicateKeyException;
import io.ebean.Transaction;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Duration;
import java.util.Date;
import java.util.HexFormat;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.util.ByteString;
import play.http.HttpEntity;
import play.libs.Json;
import play.libs.typedmap.TypedKey;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

/**
 * Makes a mutating API operation safe to retry with an {@code Idempotency-Key} request header. The
 * request header and its semantics are those of draft-ietf-httpapi-idempotency-key-header; the
 * replay marker, which the draft doesn't define, follows YBA's {@code X-} convention for custom
 * headers.
 *
 * <p>The first request with a key claims it for the caller's customer and runs the operation. A
 * successful (2xx) response is stored, and a retry with the same key and the same request gets that
 * response back, with an {@code X-IDEMPOTENT-REPLAYED: true} header, without running the operation
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
  public static final String REPLAYED_HEADER = "X-IDEMPOTENT-REPLAYED";

  /** Namespace of the task UUIDs derived from idempotency keys, see {@link #taskUuidFor}. */
  public static final UUID TASK_UUID_NAMESPACE =
      UUID.fromString("3bf373dd-d374-4df5-bdea-c3cd942ae7f4");

  // The derived task UUID the idempotent request running on this thread hasn't used yet.
  private static final TypedKey<AtomicReference<UUID>> PENDING_TASK_UUID =
      TypedKey.create("idempotencyPendingTaskUuid");

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

  /**
   * Releases the keys of requests that a restart cut off, and schedules the periodic removal of
   * expired keys. Called once at startup, before requests are served.
   */
  public void start() {
    releaseInProgressKeys();
    platformScheduler.schedule(
        getClass().getSimpleName(), Duration.ZERO, CLEANUP_INTERVAL, this::deleteExpiredKeys);
  }

  /**
   * YBA runs one active process, so at startup every in-progress key belongs to a request that died
   * with the previous process. Its client lost the response and will retry: release the key so the
   * retry runs now instead of getting 409 until the key expires.
   */
  @VisibleForTesting
  public void releaseInProgressKeys() {
    try {
      int released = ApiIdempotencyKey.deleteInProgress();
      if (released > 0) {
        log.info("Released {} idempotency keys of requests interrupted by a restart", released);
      }
    } catch (Exception e) {
      log.error("Failed to release in-progress idempotency keys", e);
    }
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

    UUID taskUUID = taskUuidFor(customer.getUuid(), key);
    Optional<TaskInfo> existingTask = TaskInfo.maybeGet(taskUUID);
    if (existingTask.isPresent()) {
      Duration retention = confGetter.getGlobalConf(GlobalConfKeys.idempotencyKeyRetention);
      boolean recent = plus(existingTask.get().getCreateTime(), retention).after(new Date());
      Optional<CustomerTask> customerTask = CustomerTask.maybeGet(taskUUID);
      // The task was started unless it failed while being submitted (param validation, a full
      // executor): that fails the request before its handler records the CustomerTask, and
      // leaves nothing to follow. A started task is followable even once it failed or was
      // aborted: a restart fails it (PLATFORM_RESTARTED) or aborts it (PLATFORM_SHUTDOWN) and may
      // retry it under a new UUID, and running the operation again would duplicate it.
      boolean started =
          customerTask.isPresent()
              || !TaskInfo.ERROR_STATES.contains(existingTask.get().getTaskState());
      if (recent && started) {
        // An earlier request with this key submitted the task, but its response was never stored:
        // YBA restarted, or the operation failed after submitting. Point the client at the task
        // instead of submitting a second one.
        release(claim.record);
        ObjectNode error = Json.newObject();
        error.put(
            "message",
            "A task was already submitted for "
                + IDEMPOTENCY_KEY_HEADER
                + " "
                + key
                + ", follow it instead of retrying");
        error.put("task_uuid", taskUUID.toString());
        customerTask
            .flatMap(IdempotencyService::latestRetry)
            .ifPresent(retry -> error.put("latest_task_uuid", retry.toString()));
        throw new PlatformServiceException(CONFLICT, error);
      }
      // A task that failed while being submitted, or one from a use of the key that expired: the
      // derived UUID is taken, so this request's task gets a random one. Reusing it would make
      // TaskExecutor reset and rerun the old task instead of creating a new one.
      taskUUID = null;
    }

    Result result;
    RequestContext.put(PENDING_TASK_UUID, new AtomicReference<>(taskUUID));
    try {
      result = operation.get();
    } catch (Throwable t) {
      release(claim.record);
      throw t;
    } finally {
      RequestContext.clean(ImmutableSet.of(PENDING_TASK_UUID));
    }
    if (isSuccess(result.status())) {
      complete(claim.record, result);
    } else {
      release(claim.record);
    }
    return result;
  }

  /**
   * The newest retry of {@code original}, if YBA or a user retried it (e.g. YBA's automatic retry
   * of a task aborted by a shutdown), so that the client follows the task that is actually running.
   * Retries are separate customer tasks on the same target, whose task params name the original.
   */
  private static Optional<UUID> latestRetry(CustomerTask original) {
    // Retries of retries carry the same root, so one match on originalTaskUUID covers the chain.
    // Filtering in the query, not over a capped list, finds the retry however many other tasks
    // (e.g. scheduled backups) the target got since.
    return CustomerTask.find
        .query()
        .where()
        .eq("customer_uuid", original.getCustomerUUID())
        .eq("target_uuid", original.getTargetUUID())
        .eq("type", original.getType())
        .gt("create_time", original.getCreateTime())
        // A primary-key probe of task_info per customer task of the target, not a JSON scan.
        .raw(
            "exists (select 1 from task_info ti where ti.uuid = t0.task_uuid"
                + " and ti.task_params->>'originalTaskUUID' = ?)",
            original.getTaskUUID().toString())
        .orderBy("create_time desc")
        .setMaxRows(1)
        .findOneOrEmpty()
        .map(CustomerTask::getTaskUUID);
  }

  /**
   * The UUID that the task an idempotent request submits gets, so that a client can follow the task
   * even when it lost the response, and a retry can't submit it twice: a name-based (version 5)
   * UUID of {@code "<customer uuid>:<key>"} in the {@link #TASK_UUID_NAMESPACE} namespace.
   */
  public static UUID taskUuidFor(UUID customerUUID, String key) {
    return nameUuidV5(TASK_UUID_NAMESPACE, customerUUID + ":" + key);
  }

  /**
   * Called by {@code Commissioner.submit} for a task submitted without an explicit UUID: returns
   * the derived task UUID of the idempotent request running on this thread, once, so only the first
   * task the operation submits gets it. Null outside such a request.
   */
  public static UUID consumeTaskUuid() {
    AtomicReference<UUID> pending = RequestContext.getIfPresent(PENDING_TASK_UUID);
    return pending == null ? null : pending.getAndSet(null);
  }

  @VisibleForTesting
  static UUID nameUuidV5(UUID namespace, String name) {
    try {
      MessageDigest sha1 = MessageDigest.getInstance("SHA-1");
      ByteBuffer namespaceBytes = ByteBuffer.allocate(16);
      namespaceBytes.putLong(namespace.getMostSignificantBits());
      namespaceBytes.putLong(namespace.getLeastSignificantBits());
      sha1.update(namespaceBytes.array());
      sha1.update(name.getBytes(StandardCharsets.UTF_8));
      byte[] hash = sha1.digest();
      hash[6] = (byte) ((hash[6] & 0x0f) | 0x50); // version 5
      hash[8] = (byte) ((hash[8] & 0x3f) | 0x80); // IETF variant
      ByteBuffer bytes = ByteBuffer.wrap(hash, 0, 16);
      return new UUID(bytes.getLong(), bytes.getLong());
    } catch (NoSuchAlgorithmException e) {
      throw new IllegalStateException("SHA-1 is not available", e);
    }
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
        deleteClaim(used.getUuid());
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
      // Still running, or orphaned by a failed key write: a restart releases the keys of the
      // requests it interrupted, and the retention ends any other. Running the request again
      // meanwhile could duplicate it.
      throw new PlatformServiceException(
          CONFLICT,
          "A request with " + IDEMPOTENCY_KEY_HEADER + " " + key + " is still in progress");
    }
    throw new PlatformServiceException(
        CONFLICT, "A concurrent request is using " + IDEMPOTENCY_KEY_HEADER + " " + key);
  }

  /**
   * Deletes a claim by its row UUID. A later claim of the same key is a new row (the expired one is
   * deleted first), so this never removes another request's claim.
   */
  private static void deleteClaim(UUID uuid) {
    DB.sqlUpdate("DELETE FROM api_idempotency_key WHERE uuid = :uuid")
        .setParameter("uuid", uuid)
        .execute();
  }

  /**
   * Stores the response on the key this request claimed. If the claim is gone meanwhile (this
   * request outlived the retention and another request reused the key under a new row), that row is
   * left alone.
   */
  @VisibleForTesting
  static void complete(ApiIdempotencyKey record, Result result) {
    UUID uuid = record.getUuid();
    try (Transaction txn = DB.beginTransaction()) {
      ApiIdempotencyKey claimed =
          ApiIdempotencyKey.find
              .query()
              .forUpdate()
              .where()
              .eq("uuid", uuid)
              .findOne();
      if (claimed == null) {
        log.warn(
            "Idempotency key {} expired while its request ran, not storing the response",
            record.getIdempotencyKey());
        return;
      }
      claimed.setResponseStatus(result.status());
      claimed.setResponseContentType(result.body().contentType().orElse(null));
      claimed.setResponseBody(bodyAsString(result));
      claimed.setState(State.COMPLETED);
      claimed.setUpdateTime(new Date());
      claimed.update();
      txn.commit();
    } catch (Exception e) {
      // The operation succeeded; failing the request now would make the client retry it and run
      // it twice. Release the key instead: a retry then runs again, as it would without a key.
      log.error(
          "Failed to store the response for idempotency key {}, releasing it",
          record.getIdempotencyKey(),
          e);
      try {
        deleteClaim(uuid);
      } catch (Exception releaseError) {
        log.error("Failed to release idempotency key {}", record.getIdempotencyKey(), releaseError);
      }
    }
  }

  private static void release(ApiIdempotencyKey record) {
    try {
      deleteClaim(record.getUuid());
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
