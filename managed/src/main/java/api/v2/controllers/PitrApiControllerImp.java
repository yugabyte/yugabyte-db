// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import static api.v2.handlers.HandlerPagingSupport.normalize;

import api.v2.handlers.HandlerPagingSupport;
import api.v2.mappers.PitrConfigMapper;
import api.v2.models.PitrConfigPagedQuerySpec;
import api.v2.models.PitrConfigPagedResp;
import api.v2.utils.NormalizedPaginationSpec;
import com.google.inject.Inject;
import com.yugabyte.yw.common.pitr.PitrConfigHelper;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import io.ebean.PagedList;
import java.util.UUID;
import play.mvc.Http.Request;

public class PitrApiControllerImp extends PitrApiControllerImpInterface {

  private final PitrConfigHelper helper;

  @Inject
  public PitrApiControllerImp(PitrConfigHelper helper) {
    this.helper = helper;
  }

  @Override
  public PitrConfigPagedResp pageListPitrConfigs(
      Request request, UUID cUUID, UUID uniUUID, PitrConfigPagedQuerySpec pitrConfigPagedQuerySpec)
      throws Exception {
    Universe universe = Universe.getOrNotFound(uniUUID, cUUID);
    NormalizedPaginationSpec normalized = normalize(pitrConfigPagedQuerySpec);
    UUID drConfigUuid =
        pitrConfigPagedQuerySpec.getFilter() != null
            ? pitrConfigPagedQuerySpec.getFilter().getDrConfigUuid()
            : null;

    PagedList<PitrConfig> page = helper.listPitrConfigsPaged(universe, drConfigUuid, normalized);

    return HandlerPagingSupport.pagedResponse(
        new PitrConfigPagedResp(), page, PitrConfigMapper.INSTANCE::toPitrConfig);
  }
}
