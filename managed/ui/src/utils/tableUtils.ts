import { XClusterTable } from '../components/xcluster';
import { YBTableRelationType } from '../redesign/helpers/constants';
import { YBTable } from '../redesign/helpers/dtos';

export const isColocatedParentTable = (table: YBTable): boolean =>
  table.relationType === YBTableRelationType.COLOCATED_PARENT_TABLE_RELATION;

export const isColocatedChildTable = (table: YBTable): boolean =>
  table.colocated && !!table.colocationParentId;

export const getTableUuid = (table: YBTable): string => table.tableID ?? table.tableUUID;

export const getTableName = (table: YBTable | XClusterTable): string =>
  isColocatedParentTable(table) ? 'Colocated Parent Table' : table.tableName ?? getTableUuid(table);
