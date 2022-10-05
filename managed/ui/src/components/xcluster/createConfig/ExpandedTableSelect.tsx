import React, { useState } from 'react';
import {
  BootstrapTable,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';

import { YBTable } from '../XClusterTypes';
import { KeyspaceRow } from './SelectTablesStep';
import { YBControlledSelect } from '../../common/forms/fields';
import YBPagination from '../../tables/YBPagination/YBPagination';
import { SortOrder } from '../constants';
import { formatBytes, tableSort } from '../ReplicationUtils';

import styles from './ExpandedTableSelect.module.scss';

interface ExpandedTableSelectProps {
  row: KeyspaceRow;
  selectedTableUUIDs: string[];
  minPageSize: number;
  handleTableSelect: (row: YBTable, isSelected: boolean) => void;
  handleAllTableSelect: (isSelected: boolean, rows: YBTable[]) => boolean;
}
const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40] as const;

export const ExpandedTableSelect = ({
  row,
  selectedTableUUIDs,
  handleTableSelect,
  handleAllTableSelect
}: ExpandedTableSelectProps) => {
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof YBTable>('tableName');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);

  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type YBTable.
      setSortField(sortName as keyof YBTable);
      setSortOrder(sortOrder);
    }
  };
  return (
    <div className={styles.expandComponent}>
      <BootstrapTable
        data={row.tables
          .sort((a, b) => tableSort<YBTable>(a, b, sortField, sortOrder, 'tableName'))
          .slice((activePage - 1) * pageSize, activePage * pageSize)}
        selectRow={{
          mode: 'checkbox',
          onSelect: handleTableSelect,
          onSelectAll: handleAllTableSelect,
          selected: selectedTableUUIDs
        }}
        options={tableOptions}
      >
        <TableHeaderColumn dataField="tableUUID" isKey={true} hidden={true} />
        <TableHeaderColumn dataField="tableName" dataSort={true}>
          Table Name
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
