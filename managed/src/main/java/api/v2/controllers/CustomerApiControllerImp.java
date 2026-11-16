// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.CustomerManagementHandler;
import api.v2.models.ApiTokenSpec;
import api.v2.models.CustomerRegistration;
import api.v2.models.CustomerRegistrationSpec;
import api.v2.models.HostCloudInfo;
import api.v2.models.UserApiToken;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http;

public class CustomerApiControllerImp extends CustomerApiControllerImpInterface {
  private final CustomerManagementHandler customerHandler;

  @Inject
  public CustomerApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      CustomerManagementHandler customerHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.customerHandler = customerHandler;
  }

  @Override
  public CustomerRegistration registerCustomer(
      Http.Request request, CustomerRegistrationSpec customerRegistrationSpec) {
    return customerHandler.registerCustomer(request, customerRegistrationSpec);
  }

  @Override
  public UserApiToken generateApiToken(
      Http.Request request, UUID cUUID, ApiTokenSpec apiTokenSpec) {
    return customerHandler.generateApiToken(request, cUUID, apiTokenSpec);
  }

  @Override
  public HostCloudInfo getHostCloudInfo(Http.Request request, UUID cUUID) {
    return customerHandler.getHostCloudInfo();
  }
}
