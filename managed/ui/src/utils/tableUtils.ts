import { XClusterReplicationTable } from '../components/xcluster';
import { XClusterTableStatus } from '../components/xcluster/constants';
import { formatUuidForXCluster } from '../components/xcluster/ReplicationUtils';
import { YBTableRelationType } from '../redesign/helpers/constants';
import { YBTable } from '../redesign/helpers/dtos';

export const isColocatedParentTable = (table: YBTable): boolean =>
  table.relationType === YBTableRelationType.COLOCATED_PARENT_TABLE_RELATION;

export const isColocatedChildTable = (table: YBTable): boolean =>
  table.colocated && !!table.colocationParentId;

export const getTableUuid = (table: YBTable | XClusterReplicationTable): string =>
  isXClusterReplicationTable(table) && table.status === XClusterTableStatus.DROPPED
    ? // tableID comes from the source universe table details which is missing for dropped tables.
      formatUuidForXCluster(table.tableUUID)
    : table.tableID ?? formatUuidForXCluster(table.tableUUID);

const isXClusterReplicationTable = (
  table: YBTable | XClusterReplicationTable
): table is XClusterReplicationTable => (table as XClusterReplicationTable).status !== undefined;

export const getTableName = (table: YBTable | XClusterReplicationTable): string => {
  if (isXClusterReplicationTable(table) && table.status === XClusterTableStatus.DROPPED) {
    return `${table.tableUUID} (Dropped table)`;
  }
  return isColocatedParentTable(table)
    ? 'Colocated Parent Table'
    : table.tableName ?? getTableUuid(table);
};
