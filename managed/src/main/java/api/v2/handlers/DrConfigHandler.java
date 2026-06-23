// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static api.v2.handlers.HandlerPagingSupport.normalize;

import api.v2.mappers.DrConfigDetailMapper;
import api.v2.mappers.DrConfigMapper;
import api.v2.models.DrConfigDbDetailPagedQuerySpec;
import api.v2.models.DrConfigDbDetailPagedResp;
import api.v2.models.DrConfigPagedQuerySpec;
import api.v2.models.DrConfigPagedResp;
import api.v2.models.DrConfigTableDetailPagedQuerySpec;
import api.v2.models.DrConfigTableDetailPagedResp;
import api.v2.utils.NormalizedPaginationSpec;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.dr.DrConfigHelper;
import com.yugabyte.yw.forms.DrConfigGetResp;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.paging.PagedResponse;
import io.ebean.PagedList;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.UUID;

@Singleton
public class DrConfigHandler {

  private final DrConfigHelper drConfigHelper;

  @Inject
  public DrConfigHandler(DrConfigHelper drConfigHelper) {
    this.drConfigHelper = drConfigHelper;
  }

  public DrConfigPagedResp pageListDrConfigs(
      UUID cUUID, UUID uniUUID, DrConfigPagedQuerySpec spec) {
    NormalizedPaginationSpec normalized = normalize(spec);
    PagedList<DrConfig> page =
        drConfigHelper.getPagedDrConfigsForUniverse(cUUID, uniUUID, normalized);

    return HandlerPagingSupport.pagedResponse(
        new DrConfigPagedResp(),
        page,
        drConfig ->
            DrConfigMapper.INSTANCE.toDrConfig(
                new DrConfigGetResp(drConfig, drConfig.getActiveXClusterConfig())));
  }

  public DrConfigTableDetailPagedResp pageListDrConfigTables(
      UUID cUUID, UUID drUUID, DrConfigTableDetailPagedQuerySpec spec) {
    DrConfigGetResp drConfigGetResp =
        drConfigHelper.getDrConfigGetRespOrNotFound(cUUID, drUUID, true);
    NormalizedPaginationSpec normalized = normalize(spec);
    Comparator<XClusterTableConfig> byTableId =
        Comparator.comparing(
            XClusterTableConfig::getTableId, Comparator.nullsLast(Comparator.naturalOrder()));
    List<XClusterTableConfig> tables = new ArrayList<>(drConfigGetResp.getTableDetails());
    tables.sort("desc".equals(normalized.order()) ? byTableId.reversed() : byTableId);
    PagedResponse<XClusterTableConfig> page =
        HandlerPagingSupport.pagedResponse(tables, normalized);
    return HandlerPagingSupport.pagedResponse(
        new DrConfigTableDetailPagedResp(), page, DrConfigDetailMapper.INSTANCE::toTableDetail);
  }

  public DrConfigDbDetailPagedResp pageListDrConfigDatabases(
      UUID cUUID, UUID drUUID, DrConfigDbDetailPagedQuerySpec spec) {
    DrConfigGetResp drConfigGetResp =
        drConfigHelper.getDrConfigGetRespOrNotFound(cUUID, drUUID, true);
    PagedList<XClusterNamespaceConfig> page =
        XClusterNamespaceConfig.getPagedList(
            drConfigGetResp.getXClusterConfigUuid(), normalize(spec));
    XClusterNamespaceConfig.enrichPagedListFrom(drConfigGetResp.getDbDetails(), page);
    return HandlerPagingSupport.pagedResponse(
        new DrConfigDbDetailPagedResp(), page, DrConfigDetailMapper.INSTANCE::toDbDetail);
  }
}
