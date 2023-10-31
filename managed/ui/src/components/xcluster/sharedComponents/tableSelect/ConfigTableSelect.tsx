import { useState } from 'react';
import {
  BootstrapTable,
  ExpandColumnComponentProps,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';
import { useQuery } from 'react-query';
import clsx from 'clsx';

import { fetchTablesInUniverse } from '../../../../actions/xClusterReplication';
import { api, universeQueryKey } from '../../../../redesign/helpers/api';
import { YBControlledSelect, YBInputField } from '../../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { hasSubstringMatch } from '../../../queries/helpers/queriesHelper';
import { formatBytes, augmentTablesWithXClusterDetails, tableSort } from '../../ReplicationUtils';
import YBPagination from '../../../tables/YBPagination/YBPagination';
import { ExpandedConfigTableSelect } from './ExpandedConfigTableSelect';
import { SortOrder, YBTableRelationType } from '../../../../redesign/helpers/constants';

import { TableType, Universe, YBTable } from '../../../../redesign/helpers/dtos';
import { XClusterTable, XClusterTableType } from '../../XClusterTypes';
import { XClusterConfig } from '../../dtos';

import styles from './ConfigTableSelect.module.scss';

interface RowItem {
  keyspace: string;
  sizeBytes: number;
  xClusterTables: XClusterTable[];
}

interface ConfigTableSelectProps {
  xClusterConfig: XClusterConfig;
  selectedTableUUIDs: string[];
  setSelectedTableUUIDs: (tableUUIDs: string[]) => void;
  configTableType: XClusterTableType;
  selectedKeyspaces: string[];
  setSelectedKeyspaces: (selectedKeyspaces: string[]) => void;
  selectionError: { title?: string; body?: string } | undefined;
  selectionWarning: { title: string; body: string } | undefined;
}

const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40] as const;

const TABLE_DESCRIPTOR = 'List of databases and tables in the source universe';

/**
 * Input component for selecting tables for xCluster configuration.
 * The state of selected tables and keyspaces is controlled externally.
 */
export const ConfigTableSelect = ({
  xClusterConfig,
  selectedTableUUIDs,
  setSelectedTableUUIDs,
  configTableType,
  selectedKeyspaces,
  setSelectedKeyspaces,
  selectionError,
  selectionWarning
}: ConfigTableSelectProps) => {
  const [keyspaceSearchTerm, setKeyspaceSearchTerm] = useState('');
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof RowItem>('keyspace');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(xClusterConfig.sourceUniverseUUID, {
      excludeColocatedTables: true
    }),
    () =>
      fetchTablesInUniverse(xClusterConfig.sourceUniverseUUID, {
        excludeColocatedTables: true
      }).then((response) => response.data)
  );
  const sourceUniverseQuery = useQuery<Universe>(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  if (
    xClusterConfig.sourceUniverseUUID === undefined ||
    xClusterConfig.targetUniverseUUID === undefined
  ) {
    const errorMessage =
      xClusterConfig.sourceUniverseUUID === undefined
        ? 'The source universe is deleted.'
        : 'The target universe is deleted.';
    return <YBErrorIndicator customErrorMessage={errorMessage} />;
  }

  if (
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle
  ) {
    return <YBLoading />;
  }

  if (sourceUniverseTablesQuery.isError || sourceUniverseQuery.isError) {
    return <YBErrorIndicator />;
  }

  const toggleTableGroup = (isSelected: boolean, rows: XClusterTable[]) => {
    if (isSelected) {
      const tableUUIDsToAdd: string[] = [];
      const currentSelectedTableUUIDs = new Set(selectedTableUUIDs);

      rows.forEach((row) => {
        if (!currentSelectedTableUUIDs.has(row.tableUUID)) {
          tableUUIDsToAdd.push(row.tableUUID);
        }
      });

      setSelectedTableUUIDs([...selectedTableUUIDs, ...tableUUIDsToAdd]);
    } else {
      const removedTables = new Set(rows.map((row) => row.tableUUID));

      setSelectedTableUUIDs(
        selectedTableUUIDs.filter((tableUUID) => !removedTables.has(tableUUID))
      );
    }
  };

  const handleAllTableSelect = (isSelected: boolean, rows: XClusterTable[]) => {
    toggleTableGroup(isSelected, rows);
    return true;
  };

  const handleTableSelect = (row: XClusterTable, isSelected: boolean) => {
    if (isSelected) {
      setSelectedTableUUIDs([...selectedTableUUIDs, row.tableUUID]);
    } else {
      setSelectedTableUUIDs([
        ...selectedTableUUIDs.filter((tableUUID: string) => tableUUID !== row.tableUUID)
      ]);
    }
  };

  const toggleKeyspaceGroup = (isSelected: boolean, rows: RowItem[]) => {
    if (isSelected) {
      const keyspacesToAdd: string[] = [];
      const currentSelectedKeyspaces = new Set(selectedKeyspaces);

      rows.forEach((row) => {
        if (!currentSelectedKeyspaces.has(row.keyspace)) {
          keyspacesToAdd.push(row.keyspace);
        }
      });
      setSelectedKeyspaces([...selectedKeyspaces, ...keyspacesToAdd]);
    } else {
      const removedKeyspaces = new Set(rows.map((row) => row.keyspace));

      setSelectedKeyspaces(
        selectedKeyspaces.filter((keyspace: string) => !removedKeyspaces.has(keyspace))
      );
    }
  };

  const handleAllKeyspaceSelect = (isSelected: boolean, rows: RowItem[]) => {
    const underlyingTables = rows.reduce((table: XClusterTable[], row) => {
      return table.concat(row.xClusterTables);
    }, []);

    toggleKeyspaceGroup(isSelected, rows);
    toggleTableGroup(isSelected, underlyingTables);
    return true;
  };

  const handleKeyspaceSelect = (row: RowItem, isSelected: boolean) => {
    if (isSelected) {
      setSelectedKeyspaces([...selectedKeyspaces, row.keyspace]);
    } else {
      setSelectedKeyspaces(selectedKeyspaces.filter((keyspace) => keyspace !== row.keyspace));
    }
    toggleTableGroup(isSelected, row.xClusterTables);
  };

  const tablesInConfig = augmentTablesWithXClusterDetails(
    sourceUniverseTablesQuery.data,
    xClusterConfig.tableDetails,
    xClusterConfig.txnTableDetails
  );

  const tablesForSelection = tablesInConfig.filter(
    (xClusterTable) =>
      xClusterTable.relationType !== YBTableRelationType.INDEX_TABLE_RELATION &&
      xClusterTable.tableType !== TableType.TRANSACTION_STATUS_TABLE_TYPE
  );
  const rowItems = getRowItemsFromTables(tablesForSelection);
  const sourceUniverse = sourceUniverseQuery.data;
  const sourceUniverseNodePrefix = sourceUniverse.universeDetails.nodePrefix;
  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type RowItem.
      setSortField(sortName as keyof RowItem);
      setSortOrder(sortOrder);
    }
  };

  const sourceUniverseUUID = xClusterConfig.sourceUniverseUUID;
  return (
    <>
      <div className={styles.tableDescriptor}>{TABLE_DESCRIPTOR}</div>
      <div className={styles.tableToolbar}>
        <YBInputField
          containerClassName={styles.keyspaceSearchInput}
          placeHolder="Search for keyspace.."
          onValueChanged={(searchTerm: string) => setKeyspaceSearchTerm(searchTerm)}
        />
      </div>
      <div className={styles.bootstrapTableContainer}>
        <BootstrapTable
          tableContainerClass={styles.bootstrapTable}
          maxHeight="450px"
          data={rowItems
            .filter((row) => hasSubstringMatch(row.keyspace, keyspaceSearchTerm))
            .sort((a, b) => tableSort<RowItem>(a, b, sortField, sortOrder, 'keyspace'))
            .slice((activePage - 1) * pageSize, activePage * pageSize)}
          expandableRow={(row: RowItem) => {
            return row.xClusterTables.length > 0;
          }}
          expandComponent={(row: RowItem) => (
            <ExpandedConfigTableSelect
              tables={row.xClusterTables}
              selectedTableUUIDs={selectedTableUUIDs}
              tableType={configTableType}
              sourceUniverseUUID={sourceUniverseUUID}
              sourceUniverseNodePrefix={sourceUniverseNodePrefix}
              handleTableSelect={handleTableSelect}
              handleAllTableSelect={handleAllTableSelect}
            />
          )}
          expandColumnOptions={{
            expandColumnVisible: true,
            expandColumnComponent: expandColumnComponent,
            columnWidth: 25
          }}
          selectRow={{
            mode: 'checkbox',
            clickToExpand: true,
            onSelect: handleKeyspaceSelect,
            onSelectAll: handleAllKeyspaceSelect,
            selected: selectedKeyspaces
          }}
          options={tableOptions}
        >
          <TableHeaderColumn dataField="keyspace" isKey={true} dataSort={true}>
            Database
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
      </div>
      {rowItems.length > TABLE_MIN_PAGE_SIZE && (
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
            numPages={Math.ceil(rowItems.length / pageSize)}
            onChange={(newPageNum: number) => {
              setActivePage(newPageNum);
            }}
            activePage={activePage}
          />
        </div>
      )}
      {configTableType === TableType.PGSQL_TABLE_TYPE ? (
        <div>
          Tables in {selectedKeyspaces.length} of {rowItems.length} database(s) selected
        </div>
      ) : (
        <div>
          {selectedTableUUIDs.length} of {tablesForSelection.length} table(s) selected
        </div>
      )}

      {(selectionError || selectionWarning) && (
        <div className={styles.validationContainer}>
          {selectionError && (
            <div className={clsx(styles.validation, styles.error)}>
              <i className="fa fa-exclamation-triangle" aria-hidden="true" />
              <div className={styles.message}>
                <h5>{selectionError.title}</h5>
                <p>{selectionError.body}</p>
              </div>
            </div>
          )}
          {selectionWarning && (
            <div className={clsx(styles.validation, styles.warning)}>
              <i className="fa fa-exclamation-triangle" aria-hidden="true" />
              <div className={styles.message}>
                <h5>{selectionWarning.title}</h5>
                <p>{selectionWarning.body}</p>
              </div>
            </div>
          )}
        </div>
      )}
    </>
  );
};

const expandColumnComponent = ({ isExpandableRow, isExpanded }: ExpandColumnComponentProps) => {
  if (!isExpandableRow) {
    return '';
  }
  return (
    <div>
      {isExpanded ? (
        <i className="fa fa-caret-up" aria-hidden="true" />
      ) : (
        <i className="fa fa-caret-down" aria-hidden="true" />
      )}
    </div>
  );
};

function getRowItemsFromTables(xClusterConfigTables: XClusterTable[]): RowItem[] {
  /**
   * Map from keyspace name to keyspace details.
   */
  const keyspaceMap = new Map<string, { xClusterTables: XClusterTable[]; sizeBytes: number }>();
  xClusterConfigTables.forEach((xClusterTable) => {
    const keyspaceDetails = keyspaceMap.get(xClusterTable.keySpace);
    if (keyspaceDetails !== undefined) {
      keyspaceDetails.xClusterTables.push(xClusterTable);
      keyspaceDetails.sizeBytes += xClusterTable.sizeBytes;
    } else {
      keyspaceMap.set(xClusterTable.keySpace, {
        xClusterTables: [xClusterTable],
        sizeBytes: xClusterTable.sizeBytes
      });
    }
  });
  return Array.from(keyspaceMap, ([keyspace, keyspaceDetails]) => ({
    keyspace,
    ...keyspaceDetails
  }));
}
