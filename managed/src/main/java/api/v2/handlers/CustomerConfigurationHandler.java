// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static api.v2.handlers.HandlerPagingSupport.normalize;

import api.v2.mappers.CustomerConfigMapper;
import api.v2.models.CustomerConfigPagedQuerySpec;
import api.v2.models.CustomerConfigPagedResp;
import api.v2.utils.NormalizedPaginationSpec;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.UUID;

public class CustomerConfigurationHandler {

  public CustomerConfigPagedResp pageListCustomerConfigs(
      UUID cUUID, CustomerConfigPagedQuerySpec spec) {
    NormalizedPaginationSpec normalized = normalize(spec);
    Customer.getOrNotFound(cUUID);

    return HandlerPagingSupport.pagedResponse(
        new CustomerConfigPagedResp(),
        CustomerConfig.getPagedList(cUUID, normalized),
        CustomerConfigMapper.INSTANCE::toCustomerConfig);
  }
}
