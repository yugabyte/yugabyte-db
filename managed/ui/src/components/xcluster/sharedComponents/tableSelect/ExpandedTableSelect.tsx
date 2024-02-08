import { useState } from 'react';
import {
  BootstrapTable,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';

import { YBControlledSelect } from '../../../common/forms/fields';
import YBPagination from '../../../tables/YBPagination/YBPagination';
import { XClusterConfigAction, XClusterTableEligibility } from '../../constants';
import { formatBytes, isTableToggleable, tableSort } from '../../ReplicationUtils';
import { TableEligibilityPill } from './TableEligibilityPill';
import { SortOrder } from '../../../../redesign/helpers/constants';

import { NamespaceItem, XClusterTableCandidate, XClusterTableType } from '../..';
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
  handleTableSelect: (row: XClusterTableCandidate, isSelected: boolean) => void;
  handleTableGroupSelect: (isSelected: boolean, rows: XClusterTableCandidate[]) => boolean;
}

export const ExpandedTableSelect = ({
  row,
  selectedTableUUIDs,
  isSelectable,
  tableType,
  xClusterConfigAction,
  handleTableSelect,
  handleTableGroupSelect
}: ExpandedTableSelectProps) => {
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof XClusterTableCandidate>('tableName');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);

  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type XClusterTableCandidate.
      setSortField(sortName as keyof XClusterTableCandidate);
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
            tableSort<XClusterTableCandidate>(a, b, sortField, sortOrder, 'tableName')
          )
          .slice((activePage - 1) * pageSize, activePage * pageSize)}
        selectRow={{
          mode: 'checkbox',
          onSelect: handleTableSelect,
          onSelectAll: handleTableGroupSelect,
          selected: selectedTableUUIDs,
          hideSelectColumn: !isSelectable,
          unselectable: untoggleableTableUuids
        }}
        options={tableOptions}
      >
        <TableHeaderColumn dataField="tableUUID" isKey={true} hidden={true} />
        <TableHeaderColumn dataField="tableName" dataSort={true} dataFormat={formatTableName}>
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

const formatTableName = (tableName: string, xClusterTable: XClusterTableCandidate) => {
  return (
    <div className={styles.tableNameContainer}>
      <div className={styles.tableName}>{tableName}</div>
      {shouldShowTableEligibilityPill(xClusterTable) && (
        <TableEligibilityPill eligibilityDetails={xClusterTable.eligibilityDetails} />
      )}
    </div>
  );
};

const shouldShowTableEligibilityPill = (xClusterTable: XClusterTableCandidate) =>
  xClusterTable.eligibilityDetails.status !== XClusterTableEligibility.ELIGIBLE_UNUSED;
