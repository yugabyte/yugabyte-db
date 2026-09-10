// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.FORBIDDEN;

import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import io.ebean.annotation.Transactional;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
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
   * Fails the request if it changes a provider that YNP owns and it does not come from YNP itself.
   *
   * @param provider the provider being changed.
   * @param request the incoming request.
   * @param operation description of the attempted change, used in the error message.
   */
  public static void checkYnpManagedProvider(
      Provider provider, Http.Request request, String operation) {
    if (!provider.isYnpManaged() || isYnpRequest(request)) {
      return;
    }
    throw new PlatformServiceException(
        FORBIDDEN,
        String.format(
            "%s is not allowed for provider %s because it is created and managed by YNP.",
            operation, provider.getName()));
  }

  /**
   * Deletes a node instance and, when the provider is YNP managed, the instance type it was the
   * last user of. Both writes happen in one transaction, so a failure cannot leave the node
   * instance deleted while its now unused instance type stays behind.
   *
   * @param provider the provider owning the node instance.
   * @param nodeInstance the node instance to delete.
   */
  @Transactional
  public static void deleteNodeInstance(Provider provider, NodeInstance nodeInstance) {
    String instanceTypeCode = nodeInstance.getInstanceTypeCode();
    nodeInstance.delete();
    removeUnusedInstanceTypes(provider, Collections.singletonList(instanceTypeCode));
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
}
