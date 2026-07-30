// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.Backup;
import api.v2.models.BackupInfo;
import api.v2.models.BackupKeyspaceTables;
import api.v2.models.BackupPointInTimeRestoreWindow;
import api.v2.models.BackupRegionLocation;
import api.v2.models.BackupSpec;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.models.BackupResp;
import com.yugabyte.yw.models.CommonBackupInfo;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import java.util.ArrayList;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.mapstruct.AfterMapping;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {DateTimeMapper.class, BackupEnumMapper.class})
public interface BackupMapper {

  BackupMapper INSTANCE = Mappers.getMapper(BackupMapper.class);

  @Mapping(target = "spec", source = ".")
  @Mapping(target = "info", source = ".")
  Backup toBackup(BackupResp source);

  @Mapping(target = "customerUuid", source = "customerUUID")
  @Mapping(target = "universeUuid", source = "universeUUID")
  @Mapping(target = "scheduleUuid", source = "scheduleUUID")
  @Mapping(target = "keyspaceTables", ignore = true)
  BackupSpec toBackupSpec(BackupResp source);

  @Mapping(target = "uuid", source = "commonBackupInfo.backupUUID")
  @Mapping(target = "baseBackupUuid", source = "commonBackupInfo.baseBackupUUID")
  @Mapping(target = "state", source = "commonBackupInfo.state")
  @Mapping(target = "storageConfigUuid", source = "commonBackupInfo.storageConfigUUID")
  @Mapping(target = "kmsConfigUuid", source = "commonBackupInfo.kmsConfigUUID")
  @Mapping(target = "taskUuid", source = "commonBackupInfo.taskUUID")
  @Mapping(target = "createTime", source = "commonBackupInfo.createTime")
  @Mapping(target = "updateTime", source = "commonBackupInfo.updateTime")
  @Mapping(target = "completionTime", source = "commonBackupInfo.completionTime")
  @Mapping(target = "totalBackupSizeInBytes", source = "commonBackupInfo.totalBackupSizeInBytes")
  @Mapping(target = "sse", source = "commonBackupInfo.sse")
  @Mapping(target = "tableByTableBackup", source = "commonBackupInfo.tableByTableBackup")
  BackupInfo toBackupInfo(BackupResp source);

  @AfterMapping
  default void mapBackupSpecKeyspaceTables(@MappingTarget BackupSpec target, BackupResp source) {
    CommonBackupInfo common = source.getCommonBackupInfo();

    if (common != null && CollectionUtils.isNotEmpty(common.getResponseList())) {
      target.setKeyspaceTables(
          common.getResponseList().stream()
              .map(this::toKeyspaceTables)
              .collect(Collectors.toList()));
    } else {
      target.setKeyspaceTables(new ArrayList<>());
    }
  }

  default BackupKeyspaceTables toKeyspaceTables(KeyspaceTablesList source) {
    if (source == null) {
      return null;
    }

    BackupKeyspaceTables out =
        new BackupKeyspaceTables()
            .keyspace(source.getKeyspace())
            .allTables(source.getAllTables())
            .tablesList(source.getTablesList())
            .tableUuidList(source.getTableUUIDList())
            .backupSizeInBytes(source.getBackupSizeInBytes())
            .defaultLocation(source.getDefaultLocation());

    if (CollectionUtils.isNotEmpty(source.getPerRegionLocations())) {
      out.setPerRegionLocations(
          source.getPerRegionLocations().stream()
              .map(this::toRegionLocation)
              .collect(Collectors.toList()));
    }

    if (source.getBackupPointInTimeRestoreWindow() != null) {
      out.setBackupPointInTimeRestoreWindow(
          toPitrWindow(source.getBackupPointInTimeRestoreWindow()));
    }

    return out;
  }

  default BackupRegionLocation toRegionLocation(BackupUtil.RegionLocations source) {
    return new BackupRegionLocation()
        .region(source.REGION)
        .location(source.LOCATION)
        .hostBase(source.HOST_BASE);
  }

  default BackupPointInTimeRestoreWindow toPitrWindow(
      com.yugabyte.yw.forms.backuprestore.BackupPointInTimeRestoreWindow source) {
    return new BackupPointInTimeRestoreWindow()
        .timestampRetentionWindowStartMillis(source.timestampRetentionWindowStartMillis)
        .timestampRetentionWindowEndMillis(source.timestampRetentionWindowEndMillis);
  }
}
