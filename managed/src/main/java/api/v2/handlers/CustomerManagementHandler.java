// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.UNAUTHORIZED;

import api.v2.models.ApiTokenSpec;
import api.v2.models.AwsHostInfo;
import api.v2.models.AzuHostInfo;
import api.v2.models.CustomerRegistration;
import api.v2.models.CustomerRegistrationSpec;
import api.v2.models.GcpHostInfo;
import api.v2.models.HostCloud;
import api.v2.models.HostCloudInfo;
import api.v2.models.UserApiToken;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Provider;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.HostCloudDetector;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.user.UserService;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.controllers.handlers.CustomerRegistrationHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import io.ebean.DB;
import io.ebean.Transaction;
import java.util.Optional;
import java.util.UUID;
import org.apache.commons.validator.routines.EmailValidator;
import play.mvc.Http;

@Singleton
public class CustomerManagementHandler {

  // TokenAuthenticator caches token lookups for 10 minutes. Play creates one per request; a
  // singleton holding one would keep accepting a token for up to 10 minutes after it is rotated.
  private final Provider<TokenAuthenticator> tokenAuthenticatorProvider;
  private final CustomerRegistrationHandler customerRegistrationHandler;
  private final UserService userService;
  private final HostCloudDetector hostCloudDetector;
  private final CloudQueryHelper cloudQueryHelper;

  @Inject
  public CustomerManagementHandler(
      Provider<TokenAuthenticator> tokenAuthenticatorProvider,
      CustomerRegistrationHandler customerRegistrationHandler,
      UserService userService,
      HostCloudDetector hostCloudDetector,
      CloudQueryHelper cloudQueryHelper) {
    this.tokenAuthenticatorProvider = tokenAuthenticatorProvider;
    this.customerRegistrationHandler = customerRegistrationHandler;
    this.userService = userService;
    this.hostCloudDetector = hostCloudDetector;
    this.cloudQueryHelper = cloudQueryHelper;
  }

  public CustomerRegistration registerCustomer(
      Http.Request request, CustomerRegistrationSpec spec) {
    if (!EmailValidator.getInstance(true /* allowLocal */).isValid(spec.getEmail())) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid email: " + spec.getEmail());
    }

    // Explicit, because ebean.mf enhances @Transactional only under com.yugabyte.yw. The
    // registration lock, the checks and the inserts must share this transaction.
    try (Transaction txn = DB.beginTransaction()) {
      int customerCount = customerRegistrationHandler.countCustomersForRegistration();
      boolean firstCustomer = customerCount == 0;
      customerRegistrationHandler
          .checkAllowed(request, customerCount)
          .ifPresent(
              refusal -> {
                switch (refusal) {
                  case SINGLE_TENANT:
                    throw new PlatformServiceException(
                        CONFLICT,
                        "A customer is already registered on this single-tenant YBA instance");
                  case SSO_ENABLED:
                    throw new PlatformServiceException(
                        CONFLICT,
                        "A customer is already registered on this SSO-enabled YBA instance");
                  case NOT_AUTHENTICATED:
                    throw new PlatformServiceException(UNAUTHORIZED, "Unable to authenticate user");
                  default:
                    throw new PlatformServiceException(
                        FORBIDDEN, "Only SuperAdmins can register additional customers");
                }
              });

      Users user =
          customerRegistrationHandler.register(
              spec.getCode(),
              spec.getName(),
              spec.getEmail(),
              spec.getPassword(),
              firstCustomer ? Users.Role.SuperAdmin : Users.Role.Admin);
      String apiToken = user.upsertApiToken();
      txn.commit();

      Customer customer = Customer.get(user.getCustomerUUID());
      if (firstCustomer) {
        // The first registration has no authenticated user to audit; attribute it to the new user.
        // put, not putIfAbsent: RequestContext is a ThreadLocal, and a user left on this worker
        // thread by an earlier request must not get the entry. A later registration was set to
        // the SuperAdmin by superAdminAuthentication.
        RequestContext.put(
            TokenAuthenticator.USER, userService.getUserWithFeatures(customer, user));
      }

      return new CustomerRegistration()
          .customerUuid(customer.getUuid())
          .userUuid(user.getUuid())
          .apiToken(apiToken)
          .apiTokenVersion(user.getApiTokenVersion());
    }
  }

  public UserApiToken generateApiToken(Http.Request request, UUID cUUID, ApiTokenSpec spec) {
    // The operation is noAuthz so that any user, whatever its role, can rotate its own token; the
    // caller is authenticated here.
    Users user = tokenAuthenticatorProvider.get().getCurrentAuthenticatedUser(request);
    if (user == null) {
      throw new PlatformServiceException(UNAUTHORIZED, "Unable to authenticate user");
    }
    Customer customer = Customer.get(cUUID);
    if (customer == null) {
      throw new PlatformServiceException(NOT_FOUND, "Customer not found: " + cUUID);
    }
    if (!customer.getUuid().equals(user.getCustomerUUID())) {
      throw new PlatformServiceException(FORBIDDEN, "User does not belong to customer " + cUUID);
    }

    // The audited user is the caller. put, not putIfAbsent: RequestContext is a ThreadLocal, and a
    // user left on this worker thread by an earlier request must not get the entry. Before the
    // rotation, so that a failure here can't kill the old token without returning a new one.
    RequestContext.put(TokenAuthenticator.USER, userService.getUserWithFeatures(customer, user));
    String apiToken = user.rotateApiToken(spec == null ? null : spec.getApiTokenVersion());

    return new UserApiToken()
        .customerUuid(customer.getUuid())
        .userUuid(user.getUuid())
        .apiToken(apiToken)
        .apiTokenVersion(user.getApiTokenVersion());
  }

  public HostCloudInfo getHostCloudInfo() {
    HostCloudInfo info = new HostCloudInfo();
    Optional<CloudType> cloud = hostCloudDetector.getHostCloud();
    if (cloud.isEmpty()) {
      return info;
    }
    info.cloud(HostCloud.fromValue(cloud.get().name()));
    JsonNode hostInfo = cloudQueryHelper.getCurrentHostInfo(cloud.get());
    // Without metadata, e.g. when the metadata service is unreachable from a pod, the cloud is
    // still known from DMI.
    if (!hostInfo.isObject() || hostInfo.has("error")) {
      return info;
    }
    switch (cloud.get()) {
      case aws:
        info.aws(
            new AwsHostInfo()
                .instanceId(text(hostInfo, "instance-id"))
                .vpcId(text(hostInfo, "vpc-id"))
                .privateIp(text(hostInfo, "privateIp"))
                .region(text(hostInfo, "region")));
        return info.region(info.getAws().getRegion());
      case gcp:
        info.gcp(
            new GcpHostInfo()
                .project(text(hostInfo, "project"))
                .network(text(hostInfo, "network"))
                .sharedVpcProject(text(hostInfo, "shared_vpc_project"))
                .zone(text(hostInfo, "zone"))
                .region(text(hostInfo, "region")));
        return info.region(info.getGcp().getRegion());
      default:
        // The Azure instance metadata document, as returned by IMDS.
        JsonNode compute = hostInfo.path("compute");
        info.azu(
            new AzuHostInfo()
                .subscriptionId(text(compute, "subscriptionId"))
                .resourceGroup(text(compute, "resourceGroupName"))
                .vmId(text(compute, "vmId"))
                .privateIp(
                    text(
                        hostInfo
                            .path("network")
                            .path("interface")
                            .path(0)
                            .path("ipv4")
                            .path("ipAddress")
                            .path(0),
                        "privateIpAddress"))
                .region(text(compute, "location")));
        return info.region(info.getAzu().getRegion());
    }
  }

  private static String text(JsonNode node, String field) {
    JsonNode value = node.path(field);
    return value.isValueNode() && !value.isNull() ? value.asText() : null;
  }
}
