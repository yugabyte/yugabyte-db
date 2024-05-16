import { useState } from 'react';
import {
  BootstrapTable,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';

import { YBControlledSelect } from '../../../common/forms/fields';
import YBPagination from '../../../tables/YBPagination/YBPagination';
import { XClusterConfigAction } from '../../constants';
import { formatBytes, isTableToggleable, tableSort } from '../../ReplicationUtils';
import { SortOrder } from '../../../../redesign/helpers/constants';
import { IndexTableList } from './IndexTableList';
import { TableNameCell } from './TableNameCell';
import { ExpandColumnComponent } from './ExpandColumnComponent';

import { NamespaceItem, MainTableReplicationCandidate, XClusterTableType } from '../..';
import { TableType } from '../../../../redesign/helpers/dtos';

import styles from './ExpandedTableSelect.module.scss';

const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40, 50, 100, 1000] as const;

interface ExpandedTableSelectProps {
  row: NamespaceItem;
  selectedTableUUIDs: string[];
  // Determines if the rows in this expanded table select are selectable.
  isSelectable: boolean;
  tableType: XClusterTableType;
  xClusterConfigAction: XClusterConfigAction;
  handleTableSelect: (row: MainTableReplicationCandidate, isSelected: boolean) => void;
  handleTableGroupSelect: (isSelected: boolean, rows: MainTableReplicationCandidate[]) => boolean;
}

export const ExpandedTableSelect = ({
  row,
  selectedTableUUIDs: selectedTableUuids,
  isSelectable,
  tableType,
  xClusterConfigAction,
  handleTableSelect,
  handleTableGroupSelect
}: ExpandedTableSelectProps) => {
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof MainTableReplicationCandidate>('tableName');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);

  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type XClusterTableCandidate.
      setSortField(sortName as keyof MainTableReplicationCandidate);
      setSortOrder(sortOrder);
    }
  };
  const untoggleableTableUuids = row.tables
    .filter((table) => !isTableToggleable(table, xClusterConfigAction))
    .map((table) => table.tableUUID);
  return (
    <div className={styles.expandComponent}>
      <BootstrapTable
        maxHeight="300px"
        tableContainerClass={styles.bootstrapTable}
        data={row.tables
          .sort((a, b) =>
            tableSort<MainTableReplicationCandidate>(a, b, sortField, sortOrder, 'tableName')
          )
          .slice((activePage - 1) * pageSize, activePage * pageSize)}
        expandableRow={(mainTableReplicationCandidate: MainTableReplicationCandidate) =>
          (mainTableReplicationCandidate.indexTables?.length ?? 0) > 0
        }
        expandComponent={(mainTableReplicationCandidate: MainTableReplicationCandidate) => (
          <IndexTableList mainTableReplicationCandidate={mainTableReplicationCandidate} />
        )}
        expandColumnOptions={{
          expandColumnVisible: true,
          expandColumnComponent: ExpandColumnComponent,
          columnWidth: 25
        }}
        selectRow={{
          mode: 'checkbox',
          clickToExpand: true,
          onSelect: handleTableSelect,
          onSelectAll: handleTableGroupSelect,
          selected: selectedTableUuids,
          hideSelectColumn: !isSelectable,
          unselectable: untoggleableTableUuids
        }}
        options={tableOptions}
      >
        <TableHeaderColumn dataField="tableUUID" isKey={true} hidden={true} />
        <TableHeaderColumn
          dataField="tableName"
          dataSort={true}
          dataFormat={(_: string, mainTableReplicationCandidate: MainTableReplicationCandidate) => (
            <TableNameCell tableReplicationCandidate={mainTableReplicationCandidate} />
          )}
        >
          Table Name
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField="pgSchemaName"
          dataSort={true}
          hidden={tableType === TableType.YQL_TABLE_TYPE}
          width="180px"
        >
          Schema Name
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField="sizeBytes"
          dataSort={true}
          width="100px"
          dataFormat={(cell) => formatBytes(cell)}
        >
          Size
        </TableHeaderColumn>
      </BootstrapTable>
      {row.tables.length > TABLE_MIN_PAGE_SIZE && (
        <div className={styles.paginationControls}>
          <YBControlledSelect
            className={styles.pageSizeInput}
            options={PAGE_SIZE_OPTIONS.map((option, idx) => (
              <option key={option} id={idx.toString()} value={option}>
                {option}
              </option>
            ))}
            selectVal={pageSize}
            onInputChanged={(event: any) => setPageSize(event.target.value)}
          />
          <YBPagination
            className={styles.yBPagination}
            numPages={Math.ceil(row.tables.length / pageSize)}
            onChange={(newPageNum: number) => {
              setActivePage(newPageNum);
            }}
            activePage={activePage}
          />
        </div>
      )}
    </div>
  );
};
