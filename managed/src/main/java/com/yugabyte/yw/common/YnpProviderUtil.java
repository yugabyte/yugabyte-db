// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.FORBIDDEN;

import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;

/**
 * Helpers for on-prem providers created by YNP.
 *
 * <p>YNP provisions the node and then registers the provider, its instance types and its node
 * instances with YBA. It owns all of that configuration, so YBA does not let a user change it -
 * everything a user could change out of band would silently drift away from what is really
 * configured on the nodes.
 */
@Slf4j
public final class YnpProviderUtil {

  /**
   * YNP sets this header on the YBA API calls it makes while provisioning a node, so that YBA can
   * tell YNP driven changes apart from user driven ones.
   *
   * <p>This is not an authentication mechanism - the call is still authenticated with the
   * customer's API token, and a user holding that token has every permission YNP has. It only keeps
   * users from changing, through the UI or the API, the configuration that YNP is responsible for.
   */
  public static final String YNP_REQUEST_HEADER = "X-YBA-YNP-REQUEST";

  private YnpProviderUtil() {}

  /** Returns true if the request was made by YNP while provisioning a node. */
  public static boolean isYnpRequest(Http.Request request) {
    return request != null
        && request.header(YNP_REQUEST_HEADER).map(Boolean::parseBoolean).orElse(false);
  }

  /**
   * Fails the request if it changes a provider that YNP owns and it does not come from YNP itself,
   * unless {@link ProviderConfKeys#allowYnpManagedProviderEdit} is set for the provider.
   *
   * @param confGetter used to read the runtime config override.
   * @param provider the provider being changed.
   * @param request the incoming request.
   * @param operation description of the attempted change, used in the error message.
   */
  public static void checkYnpManagedProvider(
      RuntimeConfGetter confGetter, Provider provider, Http.Request request, String operation) {
    if (!provider.isYnpManaged() || isYnpRequest(request)) {
      return;
    }
    if (confGetter.getConfForScope(provider, ProviderConfKeys.allowYnpManagedProviderEdit)) {
      log.info(
          "{} on YNP managed provider {} is allowed by {}",
          operation,
          provider.getUuid(),
          ProviderConfKeys.allowYnpManagedProviderEdit.getKey());
      return;
    }
    throw new PlatformServiceException(
        FORBIDDEN,
        String.format(
            "%s is not allowed for provider %s because it is created and managed by YNP.",
            operation, provider.getName()));
  }

  /**
   * Deactivates the instance types of a YNP managed provider that no longer have any node instance
   * referring to them. YNP creates an instance type per node shape it provisions, so an instance
   * type outlives its purpose as soon as the last node using it is removed.
   *
   * @param provider the provider owning the instance types.
   * @param instanceTypeCodes the instance type codes to check.
   * @return the instance type codes that were deactivated.
   */
  public static Set<String> removeUnusedInstanceTypes(
      Provider provider, Collection<String> instanceTypeCodes) {
    Set<String> removed = new HashSet<>();
    if (!provider.isYnpManaged()) {
      return removed;
    }
    for (String instanceTypeCode : instanceTypeCodes) {
      if (instanceTypeCode == null) {
        continue;
      }
      if (!NodeInstance.getByInstanceType(provider.getUuid(), instanceTypeCode).isEmpty()) {
        continue;
      }
      InstanceType instanceType = InstanceType.get(provider.getUuid(), instanceTypeCode);
      if (instanceType == null || !instanceType.isActive()) {
        continue;
      }
      log.info(
          "Removing unused instance type {} of YNP managed provider {}",
          instanceTypeCode,
          provider.getUuid());
      instanceType.setActive(false);
      instanceType.save();
      removed.add(instanceTypeCode);
    }
    return removed;
  }

  // Matches the results add_result writes in YNP's precheck script. add_result does not escape
  // quotes, so the output is not always valid JSON, and the message runs to the last quote.
  private static final Pattern PRECHECK_FAILURE_PATTERN =
      Pattern.compile(
          "(?m)\"check\": \"([^\"]*)\",\\s*\"result\": \"(FAIL|FATAL)\",\\s*\"message\":"
              + " \"(.*)\"$");

  /**
   * The error for a failed YNP preflight check. The output lists every check, passed ones first, so
   * the failed ones are named up front; the output follows only when none can be found in it.
   */
  public static String ynpPreflightFailureMessage(String nodeName, int exitCode, String output) {
    List<String> failures = new ArrayList<>();
    Matcher matcher = PRECHECK_FAILURE_PATTERN.matcher(output == null ? "" : output);
    while (matcher.find()) {
      failures.add(String.format("%s: %s", matcher.group(1), matcher.group(3)));
    }
    if (failures.isEmpty()) {
      return String.format(
          "Failed YNP preflight checks for node %s with exit code %d. Output: %s",
          nodeName, exitCode, output);
    }
    return String.format(
        "Failed YNP preflight checks for node %s: %s", nodeName, String.join("; ", failures));
  }
}
