import { YBTableRelationType } from '../redesign/helpers/constants';
import { YBTable } from '../redesign/helpers/dtos';

export const isColocatedParentTable = (table: YBTable): boolean =>
  table.relationType === YBTableRelationType.COLOCATED_PARENT_TABLE_RELATION;
