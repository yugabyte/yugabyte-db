// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.http.conditional;

import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.Optional;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.annotation.Nullable;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

/** Helpers for ETag / If-None-Match conditional GET handling. */
public final class ConditionalGets {

  public static final String HEADER_ETAG = "ETag";
  public static final String HEADER_IF_NONE_MATCH = "If-None-Match";
  public static final String HEADER_LAST_MODIFIED = "Last-Modified";
  public static final String HEADER_CACHE_CONTROL = "Cache-Control";
  public static final String CACHE_CONTROL_REVALIDATE = "private, no-cache";

  private static final String UUID_PATTERN =
      "([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})";

  private static final DateTimeFormatter HTTP_DATE =
      DateTimeFormatter.RFC_1123_DATE_TIME.withLocale(Locale.US).withZone(ZoneOffset.UTC);

  private ConditionalGets() {}

  public static String weakEtag(String resourceType, UUID resourceUUID, long revisionMillis) {
    return "W/\"" + resourceType + ":" + resourceUUID + ":" + revisionMillis + "\"";
  }

  public static ResourceRevision fromTimestamp(
      String resourceType, UUID resourceUUID, Date whenModified) {
    long millis = whenModified.getTime();
    return new ResourceRevision(weakEtag(resourceType, resourceUUID, millis), whenModified);
  }

  /**
   * Appends the YBA version to the ETag. Upgrades can change a response without advancing the
   * resource's revision (raw-SQL migrations, version fields in the body), so an ETag must not
   * survive one.
   */
  public static ResourceRevision scopedToYbaVersion(
      ResourceRevision revision, @Nullable String ybaVersion) {
    if (ybaVersion == null || ybaVersion.isEmpty()) {
      return revision;
    }
    String etag = revision.etag();
    // Insert inside the closing quote: W/"opaque" -> W/"opaque:version".
    String scoped =
        etag.substring(0, etag.length() - 1) + ":" + ybaVersion.replace("\"", "") + "\"";
    return new ResourceRevision(scoped, revision.lastModified());
  }

  public static boolean matchesIfNoneMatch(Http.Request request, ResourceRevision revision) {
    Optional<String> header = request.header(HEADER_IF_NONE_MATCH);
    return header.isPresent() && matchesIfNoneMatch(header.get(), revision.etag());
  }

  public static boolean matchesIfNoneMatch(String ifNoneMatchHeader, String currentEtag) {
    if (ifNoneMatchHeader == null || currentEtag == null) {
      return false;
    }

    String trimmed = ifNoneMatchHeader.trim();
    if ("*".equals(trimmed)) {
      return true;
    }

    for (String candidate : splitEtags(trimmed)) {
      if (etagsEqual(candidate, currentEtag)) {
        return true;
      }
    }

    return false;
  }

  public static Result notModified(ResourceRevision revision) {
    return attachCacheHeaders(Results.status(Http.Status.NOT_MODIFIED), revision);
  }

  public static Result attachCacheHeaders(Result result, ResourceRevision revision) {
    Result withHeaders =
        result
            .withHeader(HEADER_ETAG, revision.etag())
            .withHeader(HEADER_CACHE_CONTROL, CACHE_CONTROL_REVALIDATE);

    if (revision.lastModified() != null) {
      withHeaders =
          withHeaders.withHeader(HEADER_LAST_MODIFIED, formatHttpDate(revision.lastModified()));
    }

    return withHeaders;
  }

  public static Optional<Result> maybeNotModified(
      Http.Request request, Optional<ResourceRevision> revision) {
    if (revision.isPresent() && matchesIfNoneMatch(request, revision.get())) {
      return Optional.of(notModified(revision.get()));
    }

    return Optional.empty();
  }

  public static String formatHttpDate(Date date) {
    return HTTP_DATE.format(Instant.ofEpochMilli(date.getTime()));
  }

  public static Optional<CustomerAndResource> extractCustomerAndResource(
      String path, String resourcePathSegment) {
    Pattern pattern =
        Pattern.compile(
            ".*/customers/"
                + UUID_PATTERN
                + "/"
                + Pattern.quote(resourcePathSegment)
                + "/"
                + UUID_PATTERN
                + "(/.*)?");
    Matcher matcher = pattern.matcher(path);

    if (!matcher.find()) {
      return Optional.empty();
    }

    return Optional.of(
        new CustomerAndResource(
            UUID.fromString(matcher.group(1)), UUID.fromString(matcher.group(2))));
  }

  public record CustomerAndResource(UUID customerUUID, UUID resourceUUID) {}

  private static List<String> splitEtags(String header) {
    List<String> tags = new ArrayList<>();
    StringBuilder current = new StringBuilder();
    boolean inQuotes = false;

    for (int i = 0; i < header.length(); i++) {
      char c = header.charAt(i);

      if (c == '"') {
        inQuotes = !inQuotes;
        current.append(c);
      } else if (c == ',' && !inQuotes) {
        String part = current.toString().trim();

        if (!part.isEmpty()) {
          tags.add(part);
        }

        current.setLength(0);
      } else {
        current.append(c);
      }
    }

    String part = current.toString().trim();

    if (!part.isEmpty()) {
      tags.add(part);
    }

    return tags;
  }

  /** Weak and strong tags compare equal if the opaque tag value matches. */
  private static boolean etagsEqual(String a, String b) {
    return normalizeEtag(a).equals(normalizeEtag(b));
  }

  private static String normalizeEtag(String etag) {
    String t = etag.trim();

    if (t.regionMatches(true, 0, "W/", 0, 2)) {
      t = t.substring(2).trim();
    }

    return t;
  }
}
