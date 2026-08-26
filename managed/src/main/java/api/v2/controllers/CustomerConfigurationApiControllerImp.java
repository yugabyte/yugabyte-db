// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.CustomerConfigurationHandler;
import api.v2.models.CustomerConfigPagedQuerySpec;
import api.v2.models.CustomerConfigPagedResp;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http.Request;

public class CustomerConfigurationApiControllerImp
    extends CustomerConfigurationApiControllerImpInterface {

  private final CustomerConfigurationHandler handler;

  @Inject
  public CustomerConfigurationApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      CustomerConfigurationHandler handler) {
    super(auditService, config, gFlagsAuditHandler);
    this.handler = handler;
  }

  @Override
  public CustomerConfigPagedResp pageListCustomerConfigs(
      Request request, UUID cUUID, CustomerConfigPagedQuerySpec customerConfigPagedQuerySpec)
      throws Exception {
    return handler.pageListCustomerConfigs(cUUID, customerConfigPagedQuerySpec);
  }
}
