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
import { CollapsibleNote } from '../common/CollapsibleNote';

import { TableType, Universe } from '../../../redesign/helpers/dtos';
import { XClusterConfig, YBTable, XClusterTableType } from '../XClusterTypes';
import { XClusterTableEligibility } from '../common/TableEligibilityPill';

import styles from './SelectTablesStep.module.scss';

const DEFAULT_TABLE_TYPE_OPTION = {
  value: TableType.PGSQL_TABLE_TYPE,
  label: 'YSQL'
} as const;

const TABLE_TYPE_OPTIONS = [
  DEFAULT_TABLE_TYPE_OPTION,
  { value: TableType.YQL_TABLE_TYPE, label: 'YCQL' }
] as const;

const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40] as const;

const TABLE_TYPE_SELECT_STYLES = {
  container: (provided: any) => ({
    ...provided,
    width: 115
  }),
  control: (provided: any) => ({
    ...provided,
    height: 42
  })
};

const FORM_INSTRUCTION = 'Select the tables you want to replicate';
const TABLE_DESCRIPTOR = 'List of keyspaces and tables in the source universe';

const NOTE_CONTENT = (
  <p>
    <b>Note!</b>
    <p>
      Index tables are not shown. Replication for these tables will automatically be set up if the
      main table is selected.
    </p>
    <p>
      If a YSQL keyspace contains any tables considered ineligible for replication, it will not be
      selectable. Creating xCluster configurations for a subset of the tables in a YSQL keyspace is
      currently not supported.
    </p>
    <p>
      Replication is done at the table level. Selecting a keyspace simply adds all its{' '}
      <b>current</b> tables to the xCluster configuration.{' '}
      <b>
        Any tables created later on must be manually added to the xCluster configuration if
        replication is desired.
      </b>
    </p>
  </p>
);

const NOTE_EXPAND_CONTENT = (
  <div>
    <b>Which tables are considered eligible for xCluster replication?</b>
    <p>
      We have 2 criteria for <b>eligible tables</b>:
      <ol>
        <li>
          <b>Table not already in use</b>
          <p>
            The table is not involved in another xCluster configuration between the same two
            universes in the same direction.
          </p>
        </li>
        <li>
          <b>Matching table exists on target universe</b>
          <p>
            A table with the same name in the same keyspace and schema exists on the target
            universe.
          </p>
        </li>
      </ol>
      If a table fails to meet any of the above criteria, then it is considered an <b>ineligible</b>{' '}
      table for xCluster purposes.
    </p>
    <b>What are my options if I want to replicate a subset of tables from a YSQL keyspace?</b>
    <p>
      Creating xCluster configurations for a subset of the tables in a YSQL keyspace is currently
      not supported. In addition, if a YSQL keyspace contains ineligible tables, then the whole
      keyspace will not be selectable for replication. If needed, you may still use yb-admin to
      create xCluster configurations for a subset of the tables in a YSQL keyspace.
    </p>
    <p>
      Please be aware that we currently do not support backup/restore at table-level granularity for
      YSQL. The bootstrapping step involves a backup/restore of the source universe data, and
      initiating a restart replication task from the UI will involve bootstrapping. For a smooth
      experience managing the xCluster configuration from the UI, we do not recommend creating
      xCluster configurations for a subset of the tables in a YSQL keyspace.
    </p>
  </div>
);

/**
 * This type stores details of a table's eligibility for xCluster replication.
 */
export type EligibilityDetails =
  | {
      state: XClusterTableEligibility.ELIGIBLE;
    }
  | { state: XClusterTableEligibility.INELIGIBLE_IN_USE; xClusterConfigName: string }
  | { state: XClusterTableEligibility.INELIGIBLE_NO_MATCH };

/**
 * YBTable with an EligibilityDetail field
 */
export interface XClusterTable extends YBTable {
  eligibilityDetails: EligibilityDetails;
}

/**
 * Holds list of tables for a keyspace and provides extra metadata.
 */
interface KeyspaceItem {
  eligibleTables: number;
  sizeBytes: number;
  tables: XClusterTable[];
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
  const sharedXClusterConfigQueries = useQueries(
    sharedXClusterConfigUUIDs.map((UUID) => ({
      queryKey: ['Xcluster', UUID],
      queryFn: () => getXclusterConfig(UUID)
    }))
  ) as UseQueryResult<XClusterConfig>[];

  if (
    targetUniverseTablesQuery.isLoading ||
    targetUniverseTablesQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    targetUniverseQuery.isLoading ||
    targetUniverseQuery.isIdle
  ) {
    return <YBLoading />;
  }

  if (
    targetUniverseTablesQuery.isError ||
    sourceUniverseQuery.isError ||
    targetUniverseQuery.isError
  ) {
    return <YBErrorIndicator />;
  }

  const toggleTableGroup = (isSelected: boolean, rows: XClusterTable[]) => {
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

  const handleAllTableSelect = (isSelected: boolean, rows: XClusterTable[]) => {
    toggleTableGroup(isSelected, rows);
    return true;
  };

  const handleTableSelect = (row: XClusterTable, isSelected: boolean) => {
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
    const underlyingTables = rows.reduce((table: XClusterTable[], row) => {
      return table.concat(
        row.tables.filter(
          (table) => table.eligibilityDetails.state === XClusterTableEligibility.ELIGIBLE
        )
      );
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
    toggleTableGroup(
      isSelected,
      row.tables.filter(
        (table) => table.eligibilityDetails.state === XClusterTableEligibility.ELIGIBLE
      )
    );
  };

  // Casting workaround: https://github.com/JedWatson/react-select/issues/2902
  const handleTableTypeChange = (option: ValueType<typeof TABLE_TYPE_OPTIONS[number]>) => {
    setTableType((option as typeof TABLE_TYPE_OPTIONS[number])?.value);

    // Clear current item selection.
    // Form submission should only contain tables of the same type (YSQL or YCQL).
    setSelectedKeyspaces([]);
    checkedSetFieldValue('tableUUIDs', []);
  };

  const tableUUIDsInUse = Object.fromEntries(
    sharedXClusterConfigUUIDs.map((xClusterConfigUUID) => [xClusterConfigUUID, new Set<string>()])
  );
  for (const xClusterConfigQuery of sharedXClusterConfigQueries) {
    if (xClusterConfigQuery.isLoading) {
      return <YBLoading />;
    }
    if (xClusterConfigQuery.isError || xClusterConfigQuery.data === undefined) {
      return <YBErrorIndicator />;
    }
    tableUUIDsInUse[xClusterConfigQuery.data.name] = new Set<string>(
      xClusterConfigQuery.data.tables
    );
  }

  const replicationItems = getReplicationItemsFromTables(
    sourceTables,
    targetUniverseTablesQuery.data,
    tableUUIDsInUse
  );
  const bootstrapTableData = Object.entries(replicationItems[tableType].keyspaces)
    .filter(([keyspace, _]) => hasSubstringMatch(keyspace, keyspaceSearchTerm))
    .map(([keyspace, keyspaceItem]) => ({ keyspace, ...keyspaceItem }));
  const unselectableKeyspaces = getUnselectableKeyspaces(
    replicationItems[tableType].keyspaces,
    tableType
  );
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
      <div className={styles.formInstruction}>2. {FORM_INSTRUCTION}</div>
      <div className={styles.tableDescriptor}>{TABLE_DESCRIPTOR}</div>
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
          tableContainerClass={styles.bootstrapTable}
          maxHeight="450px"
          data={bootstrapTableData
            .sort((a, b) => tableSort<KeyspaceRow>(a, b, sortField, sortOrder, 'keyspace'))
            .slice((activePage - 1) * pageSize, activePage * pageSize)}
          expandableRow={(row: KeyspaceRow) => {
            return row.tables.length > 0;
          }}
          expandComponent={(row: KeyspaceRow) => (
            <ExpandedTableSelect
              row={row}
              selectedTableUUIDs={values.tableUUIDs}
              minPageSize={TABLE_MIN_PAGE_SIZE}
              handleTableSelect={handleTableSelect}
              handleAllTableSelect={handleAllTableSelect}
              hideCheckboxes={tableType === TableType.PGSQL_TABLE_TYPE}
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
            selected: selectedKeyspaces,
            unselectable: unselectableKeyspaces
          }}
          options={tableOptions}
        >
          <TableHeaderColumn dataField="keyspace" isKey={true} dataSort={true}>
            Keyspace
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
      {tableType === TableType.PGSQL_TABLE_TYPE ? (
        <div>
          Tables in {selectedKeyspaces.length} of{' '}
          {Object.keys(replicationItems.PGSQL_TABLE_TYPE.keyspaces).length} keyspaces selected
        </div>
      ) : (
        <div>
          {values.tableUUIDs.length} of {replicationItems[tableType].tableCount} tables selected
        </div>
      )}
      {errors.tableUUIDs && (
        <div className={styles.validationErrorContainer}>
          <i className="fa fa-exclamation-triangle" aria-hidden="true" />
          <div className={styles.errorMessage}>
            <h5>{errors.tableUUIDs.title}</h5>
            <p>{errors.tableUUIDs.body}</p>
          </div>
        </div>
      )}
      <CollapsibleNote noteContent={NOTE_CONTENT} expandContent={NOTE_EXPAND_CONTENT} />
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
 * - A YSQL keyspace (DBs) is not selectable if it contains any 'invalid' table.
 * - Since we provide table-level selection for YCQL, we filter out invalid YCQL
 *   tables from the options.
 */
function getReplicationItemsFromTables(
  sourceTables: YBTable[],
  targetUniverseTables: YBTable[],
  tableUUIDsInUse: { [xClusteConfigUUID: string]: Set<string> }
): ReplicationItems {
  return sourceTables.reduce(
    (items: ReplicationItems, sourceTable) => {
      const tableEligibility = getXClusterTableEligibility(
        sourceTable,
        targetUniverseTables,
        tableUUIDsInUse
      );
      const xClusterTable: XClusterTable = { eligibilityDetails: tableEligibility, ...sourceTable };
      const { tableType, keySpace: keyspace, sizeBytes, eligibilityDetails } = xClusterTable;

      // We only support `PGSQL_TABLE_TYPE` and `YQL_TABLE_TYPE` for now.
      // We also drop index tables from selection because replication will be
      // automatically if the main table is selected.
      if (
        xClusterTable.relationType !== YBTableRelationType.INDEX_TABLE_RELATION &&
        (tableType === TableType.PGSQL_TABLE_TYPE || tableType === TableType.YQL_TABLE_TYPE)
      ) {
        items[tableType].keyspaces[keyspace] = items[tableType].keyspaces[keyspace] ?? {
          eligibleTables: 0,
          sizeBytes: 0,
          tables: []
        };
        items[tableType].keyspaces[keyspace].sizeBytes += sizeBytes;
        items[tableType].keyspaces[keyspace].tables.push(xClusterTable);
        items[tableType].tableCount += 1;
        if (eligibilityDetails.state === XClusterTableEligibility.ELIGIBLE) {
          items[tableType].keyspaces[keyspace].eligibleTables += 1;
        }
      }
      return items;
    },
    {
      [TableType.PGSQL_TABLE_TYPE]: {
        keyspaces: {},
        tableCount: 0
      },
      [TableType.YQL_TABLE_TYPE]: {
        keyspaces: {},
        tableCount: 0
      }
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
 * A table is eligible for replication if all of the following holds true:
 * - there exists another table with same keyspace, table name, and schema name
 *   in target universe
 * - the table is NOT part of an existing xCluster config between the same universes
 *   in the same direction
 */
function getXClusterTableEligibility(
  sourceTable: YBTable,
  targetUniverseTables: YBTable[],
  tableUUIDsInUse: { [xClusteConfigUUID: string]: Set<string> }
): EligibilityDetails {
  const targetUniverseTableIds = new Set(
    targetUniverseTables.map((table) => getTableIdentifier(table))
  );
  if (!targetUniverseTableIds.has(getTableIdentifier(sourceTable))) {
    return { state: XClusterTableEligibility.INELIGIBLE_NO_MATCH };
  }

  for (const [xClusterConfigName, tableUUIDs] of Object.entries(tableUUIDsInUse)) {
    if (tableUUIDs.has(adaptTableUUID(sourceTable.tableUUID))) {
      return {
        state: XClusterTableEligibility.INELIGIBLE_IN_USE,
        xClusterConfigName: xClusterConfigName
      };
    }
  }

  return { state: XClusterTableEligibility.ELIGIBLE };
}

/**
 * - YSQL keyspaces are unselectable if they contain at least one ineligible table.
 * - YCQL keyspaces are unselectable if they contain no eligible table.
 */
function getUnselectableKeyspaces(
  keyspaceItems: Record<string, KeyspaceItem>,
  tableType: XClusterTableType
): string[] {
  return Object.entries(keyspaceItems)
    .filter(([_, keyspaceItem]) => {
      return tableType === TableType.PGSQL_TABLE_TYPE
        ? keyspaceItem.eligibleTables < keyspaceItem.tables.length
        : keyspaceItem.eligibleTables === 0;
    })
    .map(([keyspace, _]) => keyspace);
}
