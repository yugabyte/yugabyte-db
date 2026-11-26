// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.http.conditional;

import java.util.Date;
import java.util.Objects;
import javax.annotation.Nullable;

/**
 * Opaque revision of a single resource used for HTTP conditional GET (ETag / Last-Modified).
 *
 * <p>Semantics are "core entity row unchanged", not bit-identical API response. Derived enrichment
 * fields may change without advancing the revision.
 *
 * @param etag Weak ETag token, e.g. {@code W/"universe:{uuid}:{millis}"}.
 * @param lastModified Optional timestamp for {@code Last-Modified}; may be null.
 */
public record ResourceRevision(String etag, @Nullable Date lastModified) {
  public ResourceRevision(String etag, @Nullable Date lastModified) {
    this.etag = Objects.requireNonNull(etag, "etag");
    this.lastModified = lastModified;
  }
}
