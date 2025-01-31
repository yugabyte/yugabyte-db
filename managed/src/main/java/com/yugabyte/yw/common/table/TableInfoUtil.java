package com.yugabyte.yw.common.table;

import static com.yugabyte.yw.common.Util.SYSTEM_PLATFORM_DB;

import com.yugabyte.yw.models.XClusterConfig;
import java.util.List;
import java.util.stream.Collectors;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;

public class TableInfoUtil {
  public static boolean isColocatedParentTable(
      MasterDdlOuterClass.ListTablesResponsePB.TableInfo table) {
    return table.getRelationType() == MasterTypes.RelationType.COLOCATED_PARENT_TABLE_RELATION;
  }

  public static boolean isColocatedChildTable(
      MasterDdlOuterClass.ListTablesResponsePB.TableInfo table) {
    if (!table.hasColocatedInfo()) {
      return false;
    }
    return table.getColocatedInfo().getColocated() && !isColocatedParentTable(table);
  }

  public static boolean isIndexTable(MasterDdlOuterClass.ListTablesResponsePB.TableInfo table) {
    return table.getRelationType() == MasterTypes.RelationType.INDEX_TABLE_RELATION;
  }

  public static boolean isYsqlTable(MasterDdlOuterClass.ListTablesResponsePB.TableInfo table) {
    return table.getTableType() == CommonTypes.TableType.PGSQL_TABLE_TYPE;
  }

  public static boolean isYcqlTable(MasterDdlOuterClass.ListTablesResponsePB.TableInfo table) {
    return table.getTableType() == CommonTypes.TableType.YQL_TABLE_TYPE;
  }

  public static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getYsqlTables(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tables) {
    return tables.stream().filter(TableInfoUtil::isYsqlTable).collect(Collectors.toList());
  }

  public static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getYcqlTables(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tables) {
    return tables.stream().filter(TableInfoUtil::isYcqlTable).collect(Collectors.toList());
  }

  public static XClusterConfig.TableType getXClusterConfigTableType(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tables) {
    if (tables.isEmpty() || isYsqlTable(tables.get(0))) {
      return XClusterConfig.TableType.YSQL;
    } else if (isYcqlTable(tables.get(0))) {
      return XClusterConfig.TableType.YCQL;
    } else {
      return XClusterConfig.TableType.UNKNOWN;
    }
  }

  // YBA does not treat colocated parent tables as system tables, but db does.
  public static boolean isSystemTable(MasterDdlOuterClass.ListTablesResponsePB.TableInfo table) {
    return table.getRelationType() == MasterTypes.RelationType.SYSTEM_TABLE_RELATION
        || (table.getTableType() == CommonTypes.TableType.PGSQL_TABLE_TYPE
            && table.getNamespace().getName().equals(SYSTEM_PLATFORM_DB));
  }

  public static boolean isSystemRedis(MasterDdlOuterClass.ListTablesResponsePB.TableInfo table) {
    return table.getTableType() == CommonTypes.TableType.REDIS_TABLE_TYPE
        && table.getRelationType() == MasterTypes.RelationType.SYSTEM_TABLE_RELATION
        && table.getNamespace().getName().equals("system_redis")
        && table.getName().equals("redis");
  }
}
