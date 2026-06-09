// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static api.v2.handlers.HandlerPagingSupport.normalize;

import api.v2.mappers.KmsConfigurationMapper;
import api.v2.models.KmsConfigPagedQuerySpec;
import api.v2.models.KmsConfigPagedResp;
import api.v2.utils.NormalizedPaginationSpec;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import java.util.UUID;

public class EncryptionAtRestHandler {

  public KmsConfigPagedResp pageListKmsConfigs(UUID cUUID, KmsConfigPagedQuerySpec spec) {
    NormalizedPaginationSpec normalized = normalize(spec);
    Customer.getOrNotFound(cUUID);

    return HandlerPagingSupport.pagedResponse(
        new KmsConfigPagedResp(),
        KmsConfig.getPagedList(cUUID, normalized),
        KmsConfigurationMapper.INSTANCE::toKmsConfiguration);
  }
}
