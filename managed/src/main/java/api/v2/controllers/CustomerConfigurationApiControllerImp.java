// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.CustomerConfigurationHandler;
import api.v2.models.CustomerConfigPagedQuerySpec;
import api.v2.models.CustomerConfigPagedResp;
import com.google.inject.Inject;
import java.util.UUID;
import play.mvc.Http.Request;

public class CustomerConfigurationApiControllerImp
    extends CustomerConfigurationApiControllerImpInterface {

  private final CustomerConfigurationHandler handler;

  @Inject
  public CustomerConfigurationApiControllerImp(CustomerConfigurationHandler handler) {
    this.handler = handler;
  }

  @Override
  public CustomerConfigPagedResp pageListCustomerConfigs(
      Request request, UUID cUUID, CustomerConfigPagedQuerySpec customerConfigPagedQuerySpec)
      throws Exception {
    return handler.pageListCustomerConfigs(cUUID, customerConfigPagedQuerySpec);
  }
}
