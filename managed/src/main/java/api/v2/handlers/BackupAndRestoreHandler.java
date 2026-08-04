// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.BackupMapper;
import api.v2.mappers.GflagsMetadataMapper;
import api.v2.mappers.RestoreMapper;
import api.v2.models.BackupApiFilter;
import api.v2.models.BackupPagedQuerySpec;
import api.v2.models.BackupPagedResp;
import api.v2.models.GflagMetadata;
import api.v2.models.PaginationSpec;
import api.v2.models.RestoreApiFilter;
import api.v2.models.RestoreKeyspacePagedQuerySpec;
import api.v2.models.RestoreKeyspacePagedResp;
import api.v2.models.RestorePagedQuerySpec;
import api.v2.models.RestorePagedResp;
import api.v2.utils.NormalizedPaginationSpec;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.ybc.YbcGflags;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RestoreKeyspace;
import com.yugabyte.yw.models.filters.BackupFilter;
import com.yugabyte.yw.models.filters.RestoreFilter;
import com.yugabyte.yw.models.paging.BackupPagedApiResponse;
import com.yugabyte.yw.models.paging.BackupPagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import com.yugabyte.yw.models.paging.RestorePagedQuery;
import com.yugabyte.yw.models.paging.RestorePagedResponse;
import io.ebean.ExpressionList;
import io.ebean.PagedList;
import io.ebean.Query;
import java.time.OffsetDateTime;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
public class BackupAndRestoreHandler {

  public List<GflagMetadata> listYbcGflagsMetadata() {
    return GflagsMetadataMapper.INSTANCE.toGflagMetadataList(YbcGflags.ybcGflagsMetadata.values());
  }

  public BackupPagedResp pageListBackups(UUID cUUID, BackupPagedQuerySpec spec) {
    Customer.getOrNotFound(cUUID);
    BackupPagedApiResponse response = Backup.pagedList(toPagedQuery(cUUID, spec));
    return HandlerPagingSupport.pagedResponse(
        new BackupPagedResp(), response, BackupMapper.INSTANCE::toBackup);
  }

  public RestorePagedResp pageListRestores(UUID cUUID, RestorePagedQuerySpec spec) {
    Customer.getOrNotFound(cUUID);
    RestorePagedResponse response = pagedListRestores(toPagedQuery(cUUID, spec));
    return HandlerPagingSupport.pagedResponse(
        new RestorePagedResp(), response, RestoreMapper.INSTANCE::toRestore);
  }

  public RestorePagedResponse pagedListRestores(RestorePagedQuery pagedQuery) {
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(Restore.SortBy.createTime);
      pagedQuery.setDirection(SortDirection.DESC);
    }

    Query<Restore> query = Restore.createQueryByFilter(pagedQuery.getFilter()).query();
    return performPagedQuery(query, pagedQuery, RestorePagedResponse.class);
  }

  public RestoreKeyspacePagedResp pageListRestoreKeyspaces(
      UUID cUUID, UUID rUUID, RestoreKeyspacePagedQuerySpec spec) {
    Customer.getOrNotFound(cUUID);
    Restore restore =
        Restore.maybeGet(rUUID)
            .filter(r -> cUUID.equals(r.getCustomerUUID()))
            .orElseThrow(
                () ->
                    new PlatformServiceException(
                        NOT_FOUND, "Cannot find restore " + rUUID + " for customer " + cUUID));
    NormalizedPaginationSpec normalized = HandlerPagingSupport.normalize(spec);
    String order = spec.getDirection() == PaginationSpec.DirectionEnum.ASC ? "asc" : "desc";
    String orderBy = String.format("create_time %s, uuid %s", order, order);
    ExpressionList<RestoreKeyspace> expr =
        RestoreKeyspace.find.query().where().eq("restore_uuid", restore.getRestoreUUID());
    PagedList<RestoreKeyspace> pagedList =
        HandlerPagingSupport.getPagedList(expr, normalized, orderBy);
    return HandlerPagingSupport.pagedResponse(
        new RestoreKeyspacePagedResp(), pagedList, RestoreMapper.INSTANCE::toRestoreKeyspaceInfo);
  }

  private static RestorePagedQuery toPagedQuery(UUID cUUID, RestorePagedQuerySpec spec) {
    com.yugabyte.yw.forms.filters.RestoreApiFilter apiFilter = toFormsFilter(spec.getFilter());
    RestoreFilter filter = apiFilter.toFilter().toBuilder().customerUUID(cUUID).build();
    return HandlerPagingSupport.toPagedQuery(
        spec, new RestorePagedQuery(), Restore.SortBy.createTime, filter);
  }

  private static com.yugabyte.yw.forms.filters.RestoreApiFilter toFormsFilter(
      RestoreApiFilter filter) {
    com.yugabyte.yw.forms.filters.RestoreApiFilter formsFilter =
        new com.yugabyte.yw.forms.filters.RestoreApiFilter();

    if (filter == null) {
      return formsFilter;
    }

    if (filter.getDateRangeStart() != null) {
      formsFilter.setDateRangeStart(toDate(filter.getDateRangeStart()));
    }

    if (filter.getDateRangeEnd() != null) {
      formsFilter.setDateRangeEnd(toDate(filter.getDateRangeEnd()));
    }

    if (CollectionUtils.isNotEmpty(filter.getStates())) {
      Set<Restore.State> states =
          filter.getStates().stream()
              .map(s -> Restore.State.valueOf(s.toString()))
              .collect(Collectors.toSet());
      formsFilter.setStates(states);
    }

    if (CollectionUtils.isNotEmpty(filter.getUniverseNameList())) {
      formsFilter.setUniverseNameList(Set.copyOf(filter.getUniverseNameList()));
    }

    if (CollectionUtils.isNotEmpty(filter.getSourceUniverseNameList())) {
      formsFilter.setSourceUniverseNameList(Set.copyOf(filter.getSourceUniverseNameList()));
    }

    if (CollectionUtils.isNotEmpty(filter.getStorageConfigUuidList())) {
      formsFilter.setStorageConfigUUIDList(Set.copyOf(filter.getStorageConfigUuidList()));
    }

    if (CollectionUtils.isNotEmpty(filter.getUniverseUuidList())) {
      formsFilter.setUniverseUUIDList(Set.copyOf(filter.getUniverseUuidList()));
    }

    if (CollectionUtils.isNotEmpty(filter.getRestoreUuidList())) {
      formsFilter.setRestoreUUIDList(Set.copyOf(filter.getRestoreUuidList()));
    }

    if (Boolean.TRUE.equals(filter.getOnlyShowDeletedSourceUniverses())) {
      formsFilter.setOnlyShowDeletedSourceUniverses(true);
    }

    if (Boolean.TRUE.equals(filter.getShowHidden())) {
      formsFilter.setShowHidden(true);
    }

    return formsFilter;
  }

  private static BackupPagedQuery toPagedQuery(UUID cUUID, BackupPagedQuerySpec spec) {
    com.yugabyte.yw.forms.filters.BackupApiFilter apiFilter = toFormsFilter(spec.getFilter());
    BackupFilter filter = apiFilter.toFilter().toBuilder().customerUUID(cUUID).build();
    return HandlerPagingSupport.toPagedQuery(
        spec, new BackupPagedQuery(), Backup.SortBy.createTime, filter);
  }

  private static com.yugabyte.yw.forms.filters.BackupApiFilter toFormsFilter(
      BackupApiFilter filter) {
    com.yugabyte.yw.forms.filters.BackupApiFilter formsFilter =
        new com.yugabyte.yw.forms.filters.BackupApiFilter();

    if (filter == null) {
      return formsFilter;
    }

    if (filter.getDateRangeStart() != null) {
      formsFilter.setDateRangeStart(toDate(filter.getDateRangeStart()));
    }

    if (filter.getDateRangeEnd() != null) {
      formsFilter.setDateRangeEnd(toDate(filter.getDateRangeEnd()));
    }

    if (CollectionUtils.isNotEmpty(filter.getStates())) {
      Set<BackupState> states =
          filter.getStates().stream()
              .map(s -> BackupState.valueOf(s.toString()))
              .collect(Collectors.toSet());
      formsFilter.setStates(states);
    }

    if (CollectionUtils.isNotEmpty(filter.getKeyspaceList())) {
      formsFilter.setKeyspaceList(Set.copyOf(filter.getKeyspaceList()));
    }

    if (CollectionUtils.isNotEmpty(filter.getUniverseNameList())) {
      formsFilter.setUniverseNameList(Set.copyOf(filter.getUniverseNameList()));
    }

    if (CollectionUtils.isNotEmpty(filter.getStorageConfigUuidList())) {
      formsFilter.setStorageConfigUUIDList(Set.copyOf(filter.getStorageConfigUuidList()));
    }

    if (CollectionUtils.isNotEmpty(filter.getScheduleUuidList())) {
      formsFilter.setScheduleUUIDList(Set.copyOf(filter.getScheduleUuidList()));
    }

    if (CollectionUtils.isNotEmpty(filter.getUniverseUuidList())) {
      formsFilter.setUniverseUUIDList(Set.copyOf(filter.getUniverseUuidList()));
    }

    if (CollectionUtils.isNotEmpty(filter.getBackupUuidList())) {
      formsFilter.setBackupUUIDList(Set.copyOf(filter.getBackupUuidList()));
    }

    if (Boolean.TRUE.equals(filter.getOnlyShowDeletedUniverses())) {
      formsFilter.setOnlyShowDeletedUniverses(true);
    }

    if (Boolean.TRUE.equals(filter.getOnlyShowDeletedConfigs())) {
      formsFilter.setOnlyShowDeletedConfigs(true);
    }

    if (Boolean.TRUE.equals(filter.getShowHidden())) {
      formsFilter.setShowHidden(true);
    }

    return formsFilter;
  }

  private static Date toDate(OffsetDateTime value) {
    return Date.from(value.toInstant());
  }
}
