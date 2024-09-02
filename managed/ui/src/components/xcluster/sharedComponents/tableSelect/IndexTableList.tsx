import { useState } from 'react';
import {
  BootstrapTable,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';

import { SortOrder } from '../../../../redesign/helpers/constants';
import { YBControlledSelect } from '../../../common/forms/fields';
import YBPagination from '../../../tables/YBPagination/YBPagination';
import { formatBytes, isTableToggleable, tableSort } from '../../ReplicationUtils';
import { TableNameCell } from './TableNameCell';
import { XClusterConfigAction } from '../../constants';

import {
  IndexTableReplicationCandidate,
  MainTableReplicationCandidate,
  TableReplicationCandidate
} from '../../XClusterTypes';

import styles from './IndexTableList.module.scss';

interface IndexTableListProps {
  mainTableReplicationCandidate: MainTableReplicationCandidate;
  xClusterConfigAction: XClusterConfigAction;
  isMainTableSelectable: boolean;
  isTransactionalConfig: boolean;
  selectedTableUuids: string[];
  handleTableSelect: (row: TableReplicationCandidate, isSelected: boolean) => void;
  handleTableGroupSelect: (isSelected: boolean, rows: TableReplicationCandidate[]) => boolean;
}

const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40, 50, 100, 1000] as const;

export const IndexTableList = ({
  mainTableReplicationCandidate,
  isMainTableSelectable,
  isTransactionalConfig,
  selectedTableUuids,
  handleTableSelect,
  handleTableGroupSelect,
  xClusterConfigAction
}: IndexTableListProps) => {
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

  const indexTableRows =
    mainTableReplicationCandidate.indexTables?.sort((a, b) =>
      tableSort<MainTableReplicationCandidate>(a, b, sortField, sortOrder, 'tableName')
    ) ?? [];
  const untoggleableTableUuids = indexTableRows
    .filter(
      (table) =>
        isTransactionalConfig ||
        !isTableToggleable(table, xClusterConfigAction) ||
        xClusterConfigAction !== XClusterConfigAction.MANAGE_TABLE
    )
    .map((table) => table.tableUUID);
  const isSelectable =
    isMainTableSelectable &&
    !isTransactionalConfig &&
    xClusterConfigAction === XClusterConfigAction.MANAGE_TABLE;
  return (
    <div className={styles.expandComponent}>
      <BootstrapTable
        maxHeight="300px"
        tableContainerClass={styles.bootstrapTable}
        data={indexTableRows.slice((activePage - 1) * pageSize, activePage * pageSize)}
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
          dataFormat={(
            _: string,
            indexTableReplicationCandidate: IndexTableReplicationCandidate
          ) => <TableNameCell tableReplicationCandidate={indexTableReplicationCandidate} />}
        >
          Index Table Name
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
      {indexTableRows.length > TABLE_MIN_PAGE_SIZE && (
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
            numPages={Math.ceil(indexTableRows.length / pageSize)}
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
