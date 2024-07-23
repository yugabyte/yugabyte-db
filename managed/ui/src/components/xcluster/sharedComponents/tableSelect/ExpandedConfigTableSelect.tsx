import { useState } from 'react';
import {
  BootstrapTable,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';

import { YBControlledSelect } from '../../../common/forms/fields';
import YBPagination from '../../../tables/YBPagination/YBPagination';
import { XClusterConfigAction, XClusterTableStatus } from '../../constants';
import { formatBytes, tableSort } from '../../ReplicationUtils';
import { XClusterTableStatusLabel } from '../../XClusterTableStatusLabel';
import { SortOrder } from '../../../../redesign/helpers/constants';
import { getTableName } from '../../../../utils/tableUtils';
import { ConfigIndexTableList } from './ConfigIndexTableList';
import { ExpandColumnComponent } from './ExpandColumnComponent';

import { XClusterTableType } from '../..';
import { TableType } from '../../../../redesign/helpers/dtos';
import { MainTableRestartReplicationCandidate, XClusterTable } from '../../XClusterTypes';

import styles from './ExpandedTableSelect.module.scss';

const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40, 50, 100, 1000] as const;

interface ExpandedConfigTableSelectProps {
  tables: MainTableRestartReplicationCandidate[];
  selectedTableUuids: string[];
  tableType: XClusterTableType;
  handleTableSelect: (row: MainTableRestartReplicationCandidate, isSelected: boolean) => void;
  handleTableGroupSelect: (
    isSelected: boolean,
    rows: MainTableRestartReplicationCandidate[]
  ) => boolean;
}

export const ExpandedConfigTableSelect = ({
  tables,
  selectedTableUuids,
  tableType,
  handleTableSelect,
  handleTableGroupSelect
}: ExpandedConfigTableSelectProps) => {
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof MainTableRestartReplicationCandidate>(
    'tableName'
  );
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);

  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type XClusterTable.
      setSortField(sortName as keyof MainTableRestartReplicationCandidate);
      setSortOrder(sortOrder);
    }
  };

  const isSelectable = tableType !== TableType.PGSQL_TABLE_TYPE;
  return (
    <div className={styles.expandComponent}>
      <BootstrapTable
        maxHeight="300px"
        tableContainerClass={styles.bootstrapTable}
        data={tables
          .sort((a, b) =>
            tableSort<MainTableRestartReplicationCandidate>(a, b, sortField, sortOrder, 'tableName')
          )
          .slice((activePage - 1) * pageSize, activePage * pageSize)}
        expandableRow={(
          mainTableRestartReplicationCandidate: MainTableRestartReplicationCandidate
        ) => (mainTableRestartReplicationCandidate.indexTables?.length ?? 0) > 0}
        expandComponent={(
          mainTableRestartReplicationCandidate: MainTableRestartReplicationCandidate
        ) => (
          <ConfigIndexTableList
            mainTableRestartReplicationCandidate={mainTableRestartReplicationCandidate}
            xClusterConfigAction={XClusterConfigAction.RESTART}
            selectedTableUuids={selectedTableUuids}
            handleTableSelect={handleTableSelect}
            handleTableGroupSelect={handleTableGroupSelect}
          />
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
          hideSelectColumn: !isSelectable
        }}
        options={tableOptions}
      >
        <TableHeaderColumn dataField="tableUUID" isKey={true} hidden={true} />
        <TableHeaderColumn dataField="tableName" dataFormat={(_, table) => getTableName(table)}>
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
          dataFormat={(xClusterTableStatus: XClusterTableStatus, xClusterTable: XClusterTable) => (
            <XClusterTableStatusLabel
              status={xClusterTableStatus}
              errors={xClusterTable.replicationStatusErrors}
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
