import { XClusterReplicationTable, XClusterTableWithoutDetails } from '../components/xcluster';
import { XClusterTableStatus } from '../components/xcluster/constants';
import { formatUuidForXCluster } from '../components/xcluster/ReplicationUtils';
import { YBTableRelationType } from '../redesign/helpers/constants';
import { YBTable } from '../redesign/helpers/dtos';

export const getIsColocatedParentTable = (table: YBTable): boolean =>
  table.relationType === YBTableRelationType.COLOCATED_PARENT_TABLE_RELATION;

export const getIsColocatedChildTable = (table: YBTable): boolean =>
  table.colocated && !!table.colocationParentId;

export const getIsTableInfoMissing = (
  table: XClusterReplicationTable
): table is XClusterTableWithoutDetails =>
  table.status === XClusterTableStatus.DROPPED ||
  table.status === XClusterTableStatus.TABLE_INFO_MISSING;

export const getTableUuid = (table: YBTable | XClusterReplicationTable): string =>
  getIsXClusterReplicationTable(table) && getIsTableInfoMissing(table)
    ? // tableID comes from the source universe table details which is missing for dropped tables.
      formatUuidForXCluster(table.tableUUID)
    : table.tableID ?? formatUuidForXCluster(table.tableUUID);

export const getIsXClusterReplicationTable = (
  table: YBTable | XClusterReplicationTable
): table is XClusterReplicationTable => (table as XClusterReplicationTable).status !== undefined;

export const getTableName = (table: YBTable | XClusterReplicationTable): string => {
  if (getIsXClusterReplicationTable(table) && getIsTableInfoMissing(table)) {
    return table.status === XClusterTableStatus.DROPPED
      ? `${table.tableUUID} (Dropped table)`
      : table.tableUUID;
  }

  return getIsColocatedParentTable(table)
    ? 'Colocated Parent Table'
    : table.tableName ?? getTableUuid(table);
};
