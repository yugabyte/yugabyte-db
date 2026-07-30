// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.DrConfigDbDetail;
import api.v2.models.DrConfigTableDetail;
import api.v2.models.NamespaceInfo;
import api.v2.models.TableInfo;
import com.yugabyte.yw.forms.TableInfoForm.NamespaceInfoResp;
import com.yugabyte.yw.forms.TableInfoForm.TableInfoResp;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.XClusterTableConfig.ReplicationStatusError;
import java.util.List;
import java.util.Set;
import org.mapstruct.AfterMapping;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {DateTimeMapper.class, DrConfigDetailEnumMapper.class})
public interface DrConfigDetailMapper {

  DrConfigDetailMapper INSTANCE = Mappers.getMapper(DrConfigDetailMapper.class);

  @Mapping(target = "replicationStatusErrors", ignore = true)
  DrConfigTableDetail toTableDetail(XClusterTableConfig source);

  @AfterMapping
  default void setReplicationStatusErrors(
      @MappingTarget DrConfigTableDetail target, XClusterTableConfig source) {
    target.setReplicationStatusErrors(
        mapReplicationStatusErrors(source.getReplicationStatusErrors()));
  }

  @Mapping(target = "restoreUuid", source = "restore.restoreUUID")
  @Mapping(target = "backupUuid", source = "backup.backupUUID")
  DrConfigDbDetail toDbDetail(XClusterNamespaceConfig source);

  @Mapping(target = "tableId", source = "tableID")
  @Mapping(target = "tableUuid", source = "tableUUID")
  @Mapping(target = "keyspace", source = "keySpace")
  @Mapping(target = "indexTable", source = "isIndexTable")
  @Mapping(target = "indexTableIds", source = "indexTableIDs")
  TableInfo toTableInfo(TableInfoResp source);

  @Mapping(target = "namespaceUuid", source = "namespaceUUID")
  NamespaceInfo toNamespaceInfo(NamespaceInfoResp source);

  default List<String> mapReplicationStatusErrors(Set<ReplicationStatusError> errors) {
    if (errors == null || errors.isEmpty()) {
      return List.of();
    }
    return errors.stream().map(ReplicationStatusError::toString).toList();
  }
}
