// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.mappers.BackupMapper;
import api.v2.mappers.GflagsMetadataMapper;
import api.v2.models.BackupApiFilter;
import api.v2.models.BackupPagedQuerySpec;
import api.v2.models.BackupPagedResp;
import api.v2.models.GflagMetadata;
import com.google.inject.Singleton;
import com.yugabyte.yw.forms.ybc.YbcGflags;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.filters.BackupFilter;
import com.yugabyte.yw.models.paging.BackupPagedApiResponse;
import com.yugabyte.yw.models.paging.BackupPagedQuery;
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
