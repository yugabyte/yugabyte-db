// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import io.ebean.Finder;
import io.ebean.Model;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.Date;
import java.util.Optional;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;

/**
 * A customer's use of an {@code X-IDEMPOTENCY-KEY} on a mutating API operation. The first request
 * with a key claims the row; a successful response is stored on it so that retries with the same
 * key get that response back instead of running the operation again.
 */
@Getter
@Setter
@Entity
@Table(name = "api_idempotency_key")
public class ApiIdempotencyKey extends Model {

  public enum State {
    /** A request with this key is being processed (or its processing node died). */
    IN_PROGRESS,
    /** The request succeeded; its response is stored for replay. */
    COMPLETED
  }

  @Id private UUID uuid;

  private UUID customerUUID;

  private String idempotencyKey;

  private UUID userUUID;

  private String requestMethod;

  private String requestPath;

  private String requestFingerprint;

  @Enumerated(EnumType.STRING)
  private State state;

  private Integer responseStatus;

  private String responseContentType;

  // Stored as is: an operation whose response carries a secret must not be made idempotent, see
  // x-yba-api-idempotent in openapi_templates/server/README.txt.
  private String responseBody;

  private Date createTime;

  private Date updateTime;

  private Date expireTime;

  public static final Finder<UUID, ApiIdempotencyKey> find =
      new Finder<UUID, ApiIdempotencyKey>(ApiIdempotencyKey.class) {};

  public static Optional<ApiIdempotencyKey> find(UUID customerUUID, String idempotencyKey) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("idempotency_key", idempotencyKey)
        .findOneOrEmpty();
  }

  /** Deletes the rows whose retention ended before {@code now}; returns how many were deleted. */
  public static int deleteExpired(Date now) {
    return find.query().where().lt("expire_time", now).delete();
  }

  /** Deletes the rows of requests still in progress; returns how many were deleted. */
  public static int deleteInProgress() {
    return find.query().where().eq("state", State.IN_PROGRESS).delete();
  }
}
