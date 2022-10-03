import React, { useState } from 'react';
import {
  BootstrapTable,
  ExpandColumnComponentProps,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';
import { useQueries, useQuery, UseQueryResult } from 'react-query';
import Select, { ValueType } from 'react-select';
import { FormikErrors, FormikProps } from 'formik';

import { fetchTablesInUniverse, getXclusterConfig } from '../../../actions/xClusterReplication';
import { api } from '../../../redesign/helpers/api';
import { YBControlledSelect, YBInputField } from '../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { hasSubstringMatch } from '../../queries/helpers/queriesHelper';
import {
  adaptTableUUID,
  formatBytes,
  getSharedXClusterConfigs,
  tableSort
} from '../ReplicationUtils';
import {
  CreateXClusterConfigFormErrors,
  CreateXClusterConfigFormValues,
  FormStep
} from './CreateConfigModal';
import { SortOrder, YBTableRelationType } from '../constants';
import { ExpandedTableSelect } from './ExpandedTableSelect';
import YBPagination from '../../tables/YBPagination/YBPagination';

import { TableType, Universe } from '../../../redesign/helpers/dtos';
import { XClusterConfig, YBTable, XClusterTableType } from '../XClusterTypes';

import styles from './SelectTablesStep.module.scss';

const DEFAULT_TABLE_TYPE_OPTION = {
  value: TableType.PGSQL_TABLE_TYPE,
  label: 'YSQL databases'
} as const;

const TABLE_TYPE_OPTIONS = [
  DEFAULT_TABLE_TYPE_OPTION,
  { value: TableType.YQL_TABLE_TYPE, label: 'YCQL databases' }
] as const;

const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40] as const;

const TABLE_TYPE_SELECT_STYLES = {
  container: (provided: any) => ({
    ...provided,
    width: 200
  }),
  control: (provided: any) => ({
    ...provided,
    height: 42
  })
};

/**
 * Holds list of tables for a keyspace and provides extra metadata.
 */
interface KeyspaceItem {
  sizeBytes: number;
  tables: YBTable[];
}

export interface KeyspaceRow extends KeyspaceItem {
  keyspace: string;
}

/**
 * Structure for organizing tables by table type first and keyspace/database name second.
 */
type ReplicationItems = Record<
  XClusterTableType,
  { keyspaces: Record<string, KeyspaceItem>; tableCount: number }
>;

interface SelectTablesStepProps {
  formik: React.MutableRefObject<FormikProps<CreateXClusterConfigFormValues>>;
  sourceTables: YBTable[];
  currentUniverseUUID: string;
  currentStep: FormStep;
  setCurrentStep: (currentStep: FormStep) => void;
  tableType: XClusterTableType;
  setTableType: (tableType: XClusterTableType) => void;
  selectedKeyspaces: string[];
  setSelectedKeyspaces: (selectedKeyspaces: string[]) => void;
}

export const SelectTablesStep = ({
  formik,
  sourceTables,
  currentUniverseUUID,
  currentStep,
  setCurrentStep,
  tableType,
  setTableType,
  selectedKeyspaces,
  setSelectedKeyspaces
}: SelectTablesStepProps) => {
  const [keyspaceSearchTerm, setKeyspaceSearchTerm] = useState('');
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof KeyspaceRow>('keyspace');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);

  const { values, setFieldValue } = formik.current;

  // Casting because FormikValues and FormikError have different types.
  const errors = formik.current.errors as FormikErrors<CreateXClusterConfigFormErrors>;
  /**
   * Wrapper around setFieldValue from formik.
   * Ensure current step is set to Form.SELECT_TABLES when
   * editing the selected tables.
   */
  const checkedSetFieldValue = (fieldName: string, fieldValue: any) => {
    if (currentStep === FormStep.ENABLE_REPLICATION) {
      setCurrentStep(FormStep.SELECT_TABLES);
    }
    setFieldValue(fieldName, fieldValue);
  };

  const targetUniverseTablesQuery = useQuery<YBTable[]>(
    ['universe', values.targetUniverse.value, 'tables'],
    () => fetchTablesInUniverse(values.targetUniverse.value.universeUUID).then((res) => res.data),
    {
      enabled: !!values?.targetUniverse?.value
    }
  );

  const sourceUniverseQuery = useQuery<Universe>(['universe', currentUniverseUUID], () =>
    api.fetchUniverse(currentUniverseUUID)
  );

  const targetUniverseQuery = useQuery<Universe>(['universe', values.targetUniverse.value], () =>
    api.fetchUniverse(values.targetUniverse.value.universeUUID)
  );

  const sharedXClusterConfigUUIDs =
    sourceUniverseQuery?.data && targetUniverseQuery?.data
      ? getSharedXClusterConfigs(sourceUniverseQuery.data, targetUniverseQuery.data)
      : [];

  // The unsafe cast is needed due to issue with useQueries typing
  // Upgrading react-query to v3.28 may solve this issue: https://github.com/TanStack/query/issues/1675
  /**
   * Queries for shared xCluster config UUIDs
   */
  const xClusterConfigQueries = useQueries(
    sharedXClusterConfigUUIDs.map((UUID) => ({
      queryKey: ['Xcluster', UUID],
      queryFn: () => getXclusterConfig(UUID)
    }))
  ) as UseQueryResult<XClusterConfig>[];

  if (
    targetUniverseTablesQuery.isLoading ||
    sourceUniverseQuery.isLoading ||
    targetUniverseQuery.isLoading
  ) {
    return <YBLoading />;
  }

  if (
    targetUniverseTablesQuery.isError ||
    sourceUniverseQuery.isError ||
    targetUniverseQuery.isError ||
    targetUniverseTablesQuery.data === undefined ||
    sourceUniverseQuery.data === undefined ||
    targetUniverseQuery.data === undefined
  ) {
    return <YBErrorIndicator />;
  }

  const tableUUIDsInUse = new Set<string>();
  for (const xClusterConfigQuery of xClusterConfigQueries) {
    if (xClusterConfigQuery.isLoading) {
      return <YBLoading />;
    }
    if (xClusterConfigQuery.isError || xClusterConfigQuery.data === undefined) {
      return <YBErrorIndicator />;
    }

    xClusterConfigQuery.data.tables.forEach(tableUUIDsInUse.add, tableUUIDsInUse);
  }

  /**
   * Valid tables for xCluster replication
   */
  const validTables = getValidTablesForReplication(
    sourceTables,
    targetUniverseTablesQuery.data,
    tableUUIDsInUse
  );

  const toggleTableGroup = (isSelected: boolean, rows: YBTable[]) => {
    if (isSelected) {
      const tableUUIDsToAdd: string[] = [];
      const currentSelectedTables = new Set(values.tableUUIDs);

      rows.forEach((row) => {
        if (!currentSelectedTables.has(row.tableUUID)) {
          tableUUIDsToAdd.push(row.tableUUID);
        }
      });

      checkedSetFieldValue('tableUUIDs', [...values.tableUUIDs, ...tableUUIDsToAdd]);
    } else {
      const removedTables = new Set(rows.map((row) => row.tableUUID));

      checkedSetFieldValue(
        'tableUUIDs',
        values.tableUUIDs.filter((tableUUID) => !removedTables.has(tableUUID))
      );
    }
  };

  const handleAllTableSelect = (isSelected: boolean, rows: YBTable[]) => {
    toggleTableGroup(isSelected, rows);
    return true;
  };

  const handleTableSelect = (row: YBTable, isSelected: boolean) => {
    if (isSelected) {
      checkedSetFieldValue('tableUUIDs', [...values.tableUUIDs, row.tableUUID]);
    } else {
      checkedSetFieldValue('tableUUIDs', [
        ...values['tableUUIDs'].filter((tableUUID: string) => tableUUID !== row.tableUUID)
      ]);
    }
  };

  const toggleKeyspaceGroup = (isSelected: boolean, rows: KeyspaceRow[]) => {
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

  const handleAllKeyspaceSelect = (isSelected: boolean, rows: KeyspaceRow[]) => {
    const underlyingTables = rows.reduce((tableUUIDs: YBTable[], row) => {
      return tableUUIDs.concat(row.tables);
    }, []);

    toggleKeyspaceGroup(isSelected, rows);
    toggleTableGroup(isSelected, underlyingTables);
    return true;
  };

  const handleKeyspaceSelect = (row: KeyspaceRow, isSelected: boolean) => {
    if (isSelected) {
      setSelectedKeyspaces([...selectedKeyspaces, row.keyspace]);
    } else {
      setSelectedKeyspaces(selectedKeyspaces.filter((keyspace) => keyspace !== row.keyspace));
    }
    toggleTableGroup(isSelected, row.tables);
  };

  // Casting workaround: https://github.com/JedWatson/react-select/issues/2902
  const handleTableTypeChange = (option: ValueType<typeof TABLE_TYPE_OPTIONS[number]>) => {
    setTableType((option as typeof TABLE_TYPE_OPTIONS[number])?.value);

    // Clear current item selection.
    // Form submission should only contain tables of the same type (YSQL or YCQL).
    setSelectedKeyspaces([]);
    checkedSetFieldValue('tableUUIDs', []);
  };

  const replicationItems = getReplicationItemsFromTables(validTables);
  const bootstrapTableData = Object.entries(replicationItems[tableType].keyspaces)
    .filter(([keyspace, _]) => hasSubstringMatch(keyspace, keyspaceSearchTerm))
    .map(([keyspace, keyspaceItem]) => ({ keyspace, ...keyspaceItem }));

  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type KeyspaceRow.
      setSortField(sortName as keyof KeyspaceRow);
      setSortOrder(sortOrder);
    }
  };

  return (
    <>
      <div className={styles.formInstruction}>2. Select the tables you want to replicate</div>
      <div className={styles.infoSearch}>
        List of common tables across source and target universe
      </div>
      <div className={styles.tableToolbar}>
        <Select
          styles={TABLE_TYPE_SELECT_STYLES}
          options={TABLE_TYPE_OPTIONS}
          onChange={handleTableTypeChange}
          defaultValue={DEFAULT_TABLE_TYPE_OPTION}
        />
        <YBInputField
          containerClassName={styles.keyspaceSearchInput}
          placeHolder="Search.."
          onValueChanged={(searchTerm: string) => setKeyspaceSearchTerm(searchTerm)}
        />
      </div>
      <div className={styles.bootstrapTableContainer}>
        <BootstrapTable
          data={bootstrapTableData
            .sort((a, b) => tableSort<KeyspaceRow>(a, b, sortField, sortOrder, 'keyspace'))
            .slice((activePage - 1) * pageSize, activePage * pageSize)}
          height={'300'}
          tableContainerClass={styles.bootstrapTable}
          expandableRow={(row: KeyspaceRow) => {
            return row.tables.length > 0 && tableType === TableType.YQL_TABLE_TYPE;
          }}
          expandComponent={(row: KeyspaceRow) => (
            <ExpandedTableSelect
              row={row}
              selectedTableUUIDs={values.tableUUIDs}
              minPageSize={TABLE_MIN_PAGE_SIZE}
              handleTableSelect={handleTableSelect}
              handleAllTableSelect={handleAllTableSelect}
            />
          )}
          expandColumnOptions={{
            expandColumnVisible: tableType === TableType.YQL_TABLE_TYPE,
            expandColumnComponent: expandColumnComponent,
            columnWidth: 25
          }}
          selectRow={{
            mode: 'checkbox',
            clickToExpand: tableType === TableType.YQL_TABLE_TYPE,
            onSelect: handleKeyspaceSelect,
            onSelectAll: handleAllKeyspaceSelect,
            selected: selectedKeyspaces
          }}
          options={tableOptions}
        >
          <TableHeaderColumn dataField="keyspace" isKey={true} dataSort={true}>
            {tableType === TableType.PGSQL_TABLE_TYPE ? 'Namespace' : 'Keyspace'}
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
      {bootstrapTableData.length > TABLE_MIN_PAGE_SIZE && (
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
            numPages={Math.ceil(bootstrapTableData.length / pageSize)}
            onChange={(newPageNum: number) => {
              setActivePage(newPageNum);
            }}
            activePage={activePage}
          />
        </div>
      )}
      <div>
        {values.tableUUIDs.length} of {replicationItems[tableType].tableCount} tables selected
      </div>
      {errors.tableUUIDs && (
        <div className={styles.validationErrorContainer}>
          <i className="fa fa-exclamation-triangle" aria-hidden="true" />
          <div className={styles.errorMessage}>
            <h5>{errors.tableUUIDs.title}</h5>
            <p>{errors.tableUUIDs.body}</p>
          </div>
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

/**
 * Group tables by {@link TableType} and then by keyspace/database name.
 */
function getReplicationItemsFromTables(tables: YBTable[]): ReplicationItems {
  return tables.reduce(
    (items: ReplicationItems, table) => {
      const { tableType, keySpace: keyspace, sizeBytes } = table;
      if (tableType === TableType.PGSQL_TABLE_TYPE || tableType === TableType.YQL_TABLE_TYPE) {
        items[tableType].keyspaces[keyspace] = items[tableType].keyspaces[keyspace] ?? {
          sizeBytes: 0,
          tables: []
        };
        items[tableType].keyspaces[keyspace].sizeBytes += sizeBytes;
        items[tableType].keyspaces[keyspace].tables.push(table);
        items[tableType].tableCount += 1;
      }
      return items;
    },
    {
      [TableType.PGSQL_TABLE_TYPE]: { keyspaces: {}, tableCount: 0 },
      [TableType.YQL_TABLE_TYPE]: { keyspaces: {}, tableCount: 0 }
    }
  );
}

// Comma is not a valid identifier:
// https://www.postgresql.org/docs/9.2/sql-syntax-lexical.html#SQL-SYNTAX-IDENTIFIERS
// https://cassandra.apache.org/doc/latest/cassandra/cql/definitions.html
// Hence, by joining with commas, we avoid issues where the fields are unique individually
// but result in a string that not unique.
const getTableIdentifier = (table: YBTable): string =>
  `${table.keySpace},${table.pgSchemaName},${table.tableName}`;

/**
 * A table is valid for replication if all of the following holds true:
 * - there exists another table with same keyspace, table name, and schema name
 *   in target universe
 * - the table is NOT part of an existing xCluster config between the same universes
 *   in the same direction
 */
function getValidTablesForReplication(
  sourceUniverseTables: YBTable[],
  targetUniverseTables: YBTable[],
  tableUUIDsInUse: Set<string>
) {
  const targetUniverseTableIds = new Set(
    targetUniverseTables.map((table) => getTableIdentifier(table))
  );

  return sourceUniverseTables.filter(
    (sourceTable) =>
      sourceTable.relationType !== YBTableRelationType.INDEX_TABLE_RELATION &&
      !tableUUIDsInUse.has(adaptTableUUID(sourceTable.tableUUID)) &&
      targetUniverseTableIds.has(getTableIdentifier(sourceTable))
  );
}
