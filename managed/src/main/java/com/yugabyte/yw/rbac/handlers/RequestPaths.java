// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.rbac.handlers;

import com.yugabyte.yw.common.Util;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.UUID;
import java.util.regex.Pattern;

/**
 * Reads route parameters from a request path for authorization checks.
 *
 * <p>Authorization has to check the UUID the controller receives. So callers pass {@code
 * request.path()}, not {@code uri()}, since the query string is not part of the route; a segment
 * is decoded the way Play's router decodes a {@code :param} value; and the first {@code
 * /<segment>/<value>} pair wins, because a route's own parameters come before any trailing {@code
 * *param}, whose value can hold more segments.
 */
public final class RequestPaths {

  // "GET /universes/:uniUUID/proxy/*proxyUrl" in the root routes file.
  private static final Pattern METRICS_PROXY_PATH =
      Pattern.compile(
          String.format(
              "^/universes/%s/proxy/%s/.*$", Util.PATTERN_FOR_UUID, Util.PATTERN_FOR_HOST));

  private RequestPaths() {}

  /** Returns the UUID after the first {@code /<segment>/} in the path, or null if there is none. */
  public static UUID uuidAfter(String path, String segment) {
    String[] parts = path.split("/", -1);
    for (int i = 0; i + 1 < parts.length; i++) {
      // The router matches a route's static segments against the undecoded path.
      if (parts[i].equals(segment)) {
        return parseUuid(parts[i + 1]);
      }
    }
    return null;
  }

  /** Parses an undecoded path segment as a UUID, or returns null if it is not one. */
  public static UUID parseUuid(String rawSegment) {
    try {
      // The decoding play.core.routing.PathPattern applies to a ":param" value.
      return UUID.fromString(new URI("/" + rawSegment).getPath().substring(1));
    } catch (URISyntaxException | IllegalArgumentException e) {
      return null;
    }
  }

  /** Returns whether the path is the metrics proxy route. */
  public static boolean isMetricsProxy(String path) {
    return METRICS_PROXY_PATH.matcher(path).matches();
  }
}
