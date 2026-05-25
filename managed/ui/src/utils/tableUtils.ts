import { XClusterReplicationTable, XClusterTableWithoutDetails } from '../components/xcluster';
import { XClusterTableStatus } from '../components/xcluster/constants';
import { formatUuidForXCluster } from '../components/xcluster/ReplicationUtils';
import { SortOrder, YBTableRelationType } from '../redesign/helpers/constants';
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

/** Table shape with both SST and WAL sizes (required to compute total footprint). */
export type TableWithSstAndWalBytes = Pick<YBTable, 'sizeBytes' | 'walSizeBytes'>;

const isFiniteStorageByteField = (value: unknown): value is number =>
  typeof value === 'number' && Number.isFinite(value);

/**
 * Sum of `sizeBytes` (SST/user data) and `walSizeBytes`.
 * Returns `undefined` when either field is missing or not a finite number.
 */
export function getTableTotalStorageBytes(table: TableWithSstAndWalBytes): number | undefined {
  if (
    !isFiniteStorageByteField(table.sizeBytes) ||
    !isFiniteStorageByteField(table.walSizeBytes)
  ) {
    return undefined;
  }
  return table.sizeBytes + table.walSizeBytes;
}

/**
 * Returns a copy of `table` with `sizeBytes` set to SST + WAL total when both are valid.
 * Leaves the row unchanged when total storage cannot be computed (e.g. invalid API data).
 */
export function withTotalStorageAsSizeBytes<T extends TableWithSstAndWalBytes>(table: T): T {
  const totalStorageBytes = getTableTotalStorageBytes(table);
  if (totalStorageBytes === undefined) {
    return table;
  }
  return { ...table, sizeBytes: totalStorageBytes };
}

/**
 * Compare two optional byte totals for table sorting. Rows with `undefined` size always sort last.
 */
export function compareTotalStorageBytesForSort(
  aBytes: number | undefined,
  bBytes: number | undefined,
  order: SortOrder
): number {
  if (aBytes === undefined && bBytes === undefined) {
    return 0;
  }
  if (aBytes === undefined) {
    return 1;
  }
  if (bBytes === undefined) {
    return -1;
  }
  return order === SortOrder.ASCENDING ? aBytes - bBytes : bBytes - aBytes;
}

/**
 * Total stored bytes for an xCluster replication table row when {@link XClusterTable} (YBTable)
 * details exist. Returns `undefined` for {@link XClusterTableWithoutDetails} (dropped /
 * table-info-missing).
 */
export function getXClusterReplicationTableTotalStorageBytes(
  table: XClusterReplicationTable
): number | undefined {
  if (getIsTableInfoMissing(table)) {
    return undefined;
  }
  return getTableTotalStorageBytes(table);
}
