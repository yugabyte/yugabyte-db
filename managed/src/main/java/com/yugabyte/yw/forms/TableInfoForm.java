// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Builder;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import lombok.extern.jackson.Jacksonized;
import org.yb.CommonTypes;
import org.yb.master.MasterTypes;

public class TableInfoForm {

  @ApiModel(description = "Table information response")
  @Builder
  @Jacksonized
  public static class TableInfoResp {

    @ApiModelProperty(value = "Table ID", accessMode = READ_ONLY)
    public final String tableID;

    @ApiModelProperty(value = "Table UUID", accessMode = READ_ONLY)
    public final UUID tableUUID;

    @ApiModelProperty(value = "Keyspace")
    public final String keySpace;

    @ApiModelProperty(value = "Table type")
    public final CommonTypes.TableType tableType;

    @ApiModelProperty(value = "Table name")
    public final String tableName;

    @ApiModelProperty(value = "Relation type")
    public final MasterTypes.RelationType relationType;

    @ApiModelProperty(value = "SST size in bytes", accessMode = READ_ONLY)
    public final double sizeBytes;

    @ApiModelProperty(value = "WAL size in bytes", accessMode = READ_ONLY)
    public final double walSizeBytes;

    @ApiModelProperty(value = "UI_ONLY", hidden = true)
    public final boolean isIndexTable;

    @ApiModelProperty(value = "Namespace or Schema")
    public final String nameSpace;

    @ApiModelProperty(value = "Table space")
    public final String tableSpace;

    @ApiModelProperty(value = "Parent Table UUID")
    public final UUID parentTableUUID;

    @ApiModelProperty(value = "Main Table UUID of index tables")
    public final UUID mainTableUUID;

    @ApiModelProperty(value = "Postgres schema name of the table", example = "public")
    public final String pgSchemaName;

    @ApiModelProperty(value = "Flag, indicating colocated table")
    public final Boolean colocated;

    @ApiModelProperty(value = "Colocation parent id")
    public final String colocationParentId;

    @JsonIgnore
    public boolean isColocatedChildTable() {
      // Colocated parent tables do not have ParentTableId set.
      if (this.colocated
          && this.colocationParentId != null
          && this.relationType != MasterTypes.RelationType.COLOCATED_PARENT_TABLE_RELATION) {
        return true;
      }
      return false;
    }
  }

  @ApiModel(description = "Namespace information response")
  @Builder
  @Jacksonized
  public static class NamespaceInfoResp {

    @ApiModelProperty(value = "Namespace UUID", accessMode = READ_ONLY)
    public final UUID namespaceUUID;

    @ApiModelProperty(value = "Namespace name")
    public final String name;

    @ApiModelProperty(value = "Table type")
    public final CommonTypes.TableType tableType;

    public static NamespaceInfoResp createFromNamespaceIdentifier(
        MasterTypes.NamespaceIdentifierPB namespaceIdentifier) {
      return buildResponseFromNamespaceIdentifier(namespaceIdentifier).build();
    }

    private static NamespaceInfoResp.NamespaceInfoRespBuilder buildResponseFromNamespaceIdentifier(
        MasterTypes.NamespaceIdentifierPB namespace) {
      String id = namespace.getId().toStringUtf8();
      NamespaceInfoResp.NamespaceInfoRespBuilder builder =
          NamespaceInfoResp.builder()
              .namespaceUUID(getUUIDRepresentation(id))
              .tableType(
                  BackupUtil.TABLE_TYPE_TO_YQL_DATABASE_MAP
                      .inverse()
                      .get(namespace.getDatabaseType()))
              .name(namespace.getName());
      return builder;
    }
  }

  @Data
  public static class TableSizes {
    private double sstSizeBytes;
    private double walSizeBytes;
  }

  @ToString
  public static class TablePartitionInfo {

    public String tableName;

    public String schemaName;

    public String tablespace;

    public String parentTable;

    public String parentSchema;

    public String parentTablespace;

    public String keyspace;

    public TablePartitionInfo() {}

    public TablePartitionInfoKey getKey() {
      return new TablePartitionInfoKey(tableName, keyspace);
    }
  }

  @EqualsAndHashCode
  public static class TablePartitionInfoKey {
    private final String tableName;
    private final String keyspace;

    public TablePartitionInfoKey(String tableName, String keyspace) {
      this.tableName = tableName;
      this.keyspace = keyspace;
    }
  }
}
