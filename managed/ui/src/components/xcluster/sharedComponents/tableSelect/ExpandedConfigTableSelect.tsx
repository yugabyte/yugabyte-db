import { useState } from 'react';
import {
  BootstrapTable,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';

import { YBControlledSelect } from '../../../common/forms/fields';
import YBPagination from '../../../tables/YBPagination/YBPagination';
import { XClusterTableStatus } from '../../constants';
import { formatBytes, tableSort } from '../../ReplicationUtils';

import { XClusterTableType } from '../..';
import { TableType } from '../../../../redesign/helpers/dtos';

import styles from './ExpandedTableSelect.module.scss';
import { XClusterTable } from '../../XClusterTypes';
import { XClusterTableStatusLabel } from '../../XClusterTableStatusLabel';
import { SortOrder } from '../../../../redesign/helpers/constants';

const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40, 50, 100, 1000] as const;

interface ExpandedConfigTableSelectProps {
  tables: XClusterTable[];
  selectedTableUUIDs: string[];
  tableType: XClusterTableType;
  sourceUniverseUUID: string;
  sourceUniverseNodePrefix: string;
  handleTableSelect: (row: XClusterTable, isSelected: boolean) => void;
  handleAllTableSelect: (isSelected: boolean, rows: XClusterTable[]) => boolean;
}

export const ExpandedConfigTableSelect = ({
  tables,
  selectedTableUUIDs,
  tableType,
  sourceUniverseUUID,
  sourceUniverseNodePrefix,
  handleTableSelect,
  handleAllTableSelect
}: ExpandedConfigTableSelectProps) => {
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof XClusterTable>('tableName');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);

  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type XClusterTable.
      setSortField(sortName as keyof XClusterTable);
      setSortOrder(sortOrder);
    }
  };

  return (
    <div className={styles.expandComponent}>
      <BootstrapTable
        maxHeight="300px"
        tableContainerClass={styles.bootstrapTable}
        data={tables
          .sort((a, b) => tableSort<XClusterTable>(a, b, sortField, sortOrder, 'tableName'))
          .slice((activePage - 1) * pageSize, activePage * pageSize)}
        selectRow={{
          mode: 'checkbox',
          onSelect: handleTableSelect,
          onSelectAll: handleAllTableSelect,
          selected: selectedTableUUIDs,
          hideSelectColumn: tableType === TableType.PGSQL_TABLE_TYPE
        }}
        options={tableOptions}
      >
        <TableHeaderColumn dataField="tableUUID" isKey={true} hidden={true} />
        <TableHeaderColumn dataField="tableName" dataSort={true}>
          Table Name
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField="pgSchemaName"
          dataSort={true}
          hidden={tableType === TableType.YQL_TABLE_TYPE}
        >
          Schema Name
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField="status"
          dataFormat={(cell: XClusterTableStatus, row: XClusterTable) => (
            <XClusterTableStatusLabel
              status={cell}
              streamId={row.streamId}
              sourceUniverseTableUuid={row.tableUUID}
              sourceUniverseNodePrefix={sourceUniverseNodePrefix}
              sourceUniverseUuid={sourceUniverseUUID}
            />
          )}
        >
          Status
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
      {tables.length > TABLE_MIN_PAGE_SIZE && (
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
            numPages={Math.ceil(tables.length / pageSize)}
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
