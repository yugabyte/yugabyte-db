/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.FORBIDDEN;

import play.mvc.Http;

/**
 * Utility class for checking if an HTTP request originates from localhost. Used to restrict
 * sensitive APIs to only be accessible from the same machine where YBA is running.
 *
 * <p>Security model: Localhost check ensures request comes from 127.0.0.1, ::1, or equivalent
 * Combined with API token authentication for user identity Combined with RBAC permission checking
 * for authorization
 */
public class LocalhostAccessChecker {

  private static final String[] LOCALHOST_ADDRESSES = {
    "127.0.0.1", "::1", "0:0:0:0:0:0:0:1", "localhost"
  };

  /**
   * Checks if the request originates from localhost. Throws PlatformServiceException with FORBIDDEN
   * status if not from localhost.
   *
   * @param request The HTTP request to check
   * @throws PlatformServiceException if request is not from localhost
   */
  public void checkLocalhost(Http.Request request) {
    if (!isLocalhost(request)) {
      throw new PlatformServiceException(
          FORBIDDEN,
          String.format(
              "This API is only accessible from localhost. Remote address: %s",
              request.remoteAddress()));
    }
  }

  /**
   * Checks if the request originates from localhost.
   *
   * @param request The HTTP request to check
   * @return true if request is from localhost, false otherwise
   */
  public boolean isLocalhost(Http.Request request) {
    return isLocalhostAddress(request.remoteAddress());
  }

  /**
   * Checks if the given address is a localhost address.
   *
   * @param address The remote address to check
   * @return true if the address is localhost, false otherwise
   */
  public static boolean isLocalhostAddress(String address) {
    if (address == null) {
      return false;
    }

    String normalizedAddress = address.toLowerCase().trim();

    // Check common localhost addresses
    for (String localhost : LOCALHOST_ADDRESSES) {
      if (normalizedAddress.equals(localhost)) {
        return true;
      }
    }

    // Handle IPv6 addresses with zone identifiers (e.g., "::1%lo")
    if (normalizedAddress.startsWith("::1") || normalizedAddress.startsWith("0:0:0:0:0:0:0:1")) {
      return true;
    }

    return false;
  }
}
