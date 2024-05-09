import { useState } from 'react';
import {
  BootstrapTable,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';
import { useQueries, useQuery, UseQueryResult } from 'react-query';
import Select, { ValueType } from 'react-select';
import { Box, Typography, useTheme } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';

import {
  fetchTablesInUniverse,
  fetchXClusterConfig
} from '../../../../actions/xClusterReplication';
import {
  api,
  runtimeConfigQueryKey,
  universeQueryKey,
  xClusterQueryKey
} from '../../../../redesign/helpers/api';
import { YBCheckBox, YBControlledSelect, YBInputField } from '../../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { hasSubstringMatch } from '../../../queries/helpers/queriesHelper';
import {
  formatBytes,
  getSharedXClusterConfigs,
  tableSort,
  hasLinkedXClusterConfig,
  isTableToggleable,
  formatUuidForXCluster
} from '../../ReplicationUtils';
import {
  TRANSACTIONAL_ATOMICITY_YB_SOFTWARE_VERSION_THRESHOLD,
  XClusterConfigAction,
  XCLUSTER_REPLICATION_DOCUMENTATION_URL,
  XCLUSTER_SUPPORTED_TABLE_TYPES,
  XCLUSTER_TABLE_INELIGIBLE_STATUSES,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../../constants';
import YBPagination from '../../../tables/YBPagination/YBPagination';
import { CollapsibleNote } from '../CollapsibleNote';
import { ExpandedTableSelect } from './ExpandedTableSelect';
import { XClusterTableEligibility } from '../../constants';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import {
  RuntimeConfigKey,
  SortOrder,
  YBTableRelationType
} from '../../../../redesign/helpers/constants';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../../../actions/customers';
import { YBTooltip } from '../../../../redesign/components';
import InfoMessageIcon from '../../../../redesign/assets/info-message.svg';
import { compareYBSoftwareVersions, getPrimaryCluster } from '../../../../utils/universeUtilsTyped';
import { ExpandColumnComponent } from './ExpandColumnComponent';
import { getTableUuid } from '../../../../utils/tableUtils';
import { YBBanner, YBBannerVariant } from '../../../common/descriptors';

import {
  TableType,
  TableTypeLabel,
  Universe,
  UniverseNamespace,
  YBTable
} from '../../../../redesign/helpers/dtos';
import {
  IndexTableReplicationCandidate,
  TableReplicationCandidate,
  XClusterTableType
} from '../../XClusterTypes';
import { XClusterConfig } from '../../dtos';
import {
  EligibilityDetails,
  NamespaceItem,
  ReplicationItems,
  MainTableReplicationCandidate
} from '../..';

import styles from './TableSelect.module.scss';

interface CommonTableSelectProps {
  sourceUniverseUUID: string;
  targetUniverseUUID: string;
  selectedTableUUIDs: string[];
  setSelectedTableUUIDs: (tableUUIDs: string[]) => void;
  isDrInterface: boolean;
  isFixedTableType: boolean;
  tableType: XClusterTableType;
  setTableType: (tableType: XClusterTableType) => void;
  initialNamespaceUuids: string[];
  selectedNamespaceUuids: string[];
  setSelectedNamespaceUuids: (selectedNamespaceUuids: string[]) => void;
  selectionError: { title?: string; body?: string } | undefined;
  selectionWarning: { title: string; body: string } | undefined;
}

export type TableSelectProps =
  | (CommonTableSelectProps & {
      configAction: typeof XClusterConfigAction.CREATE;
      isTransactionalConfig: boolean;
      handleTransactionalConfigCheckboxClick: () => void;
    })
  | (CommonTableSelectProps & {
      configAction:
        | typeof XClusterConfigAction.ADD_TABLE
        | typeof XClusterConfigAction.MANAGE_TABLE;
      xClusterConfigUUID: string;
    });

const DEFAULT_TABLE_TYPE_OPTION = {
  value: TableType.PGSQL_TABLE_TYPE,
  label: TableTypeLabel[TableType.PGSQL_TABLE_TYPE]
} as const;
const TABLE_TYPE_OPTIONS = [
  DEFAULT_TABLE_TYPE_OPTION,
  { value: TableType.YQL_TABLE_TYPE, label: TableTypeLabel[TableType.YQL_TABLE_TYPE] }
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

const NOTE_CONTENT = (
  <div>
    <p>
      <b>Note! </b>
      Tables in an xCluster configuration must all be of the same type (YCQL or YSQL). Please create
      a separate xCluster configuration if you wish to replicate tables of a different type.
    </p>
    <p>
      Selecting or deselecting main tables will select and deselect their associated index tables.
    </p>
    <p>
      If a YSQL database contains any tables considered ineligible for replication, it will not be
      selectable. Creating xCluster configurations for a subset of the tables in a YSQL database is
      currently not supported.
    </p>
    <p>
      Replication is done at the table level. Selecting a database simply adds all its{' '}
      <b>current</b> tables to the xCluster configuration.{' '}
      <b>
        Any tables created later on must be manually added to the xCluster configuration if
        replication is desired.
      </b>
    </p>
  </div>
);

const NOTE_EXPAND_CONTENT = (
  <div>
    <b>Which tables are considered eligible for xCluster replication?</b>
    <p>
      We have the following criteria for <b>eligible tables</b>:
      <ul>
        <li style={{ listStyle: 'disc' }}>
          <b>Table not already in use</b>
          <p>
            The table is not involved in another xCluster configuration between the same two
            universes in the same direction.
          </p>
        </li>
      </ul>
      If a table fails to meet any of the above criteria, then it is considered an <b>ineligible</b>{' '}
      table for xCluster purposes.
    </p>
    <b>What are my options if I want to replicate a subset of tables from a YSQL database?</b>
    <p>
      Creating xCluster configurations for a subset of the tables in a YSQL database is currently
      not supported. In addition, if a YSQL database contains ineligible tables, then the whole
      database will not be selectable for replication. If needed, you may still use yb-admin to
      create xCluster configurations for a subset of the tables in a YSQL database.
    </p>
    <p>
      Please be aware that we currently do not support backup/restore at table-level granularity for
      YSQL. The full copy step involves a backup/restore of the source universe data, and initiating
      a restart replication task from the UI will involve creating a full copy. For a smooth
      experience managing the xCluster configuration from the UI, we do not recommend creating
      xCluster configurations for a subset of the tables in a YSQL database.
    </p>
  </div>
);
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.selectTable';

/**
 * Input component for selecting tables for xCluster configuration.
 * The state of selected tables and namespaces is controlled externally.
 */
export const TableSelect = (props: TableSelectProps) => {
  const {
    sourceUniverseUUID,
    targetUniverseUUID,
    selectedTableUUIDs,
    setSelectedTableUUIDs,
    tableType,
    isDrInterface,
    isFixedTableType,
    setTableType,
    initialNamespaceUuids,
    selectedNamespaceUuids,
    setSelectedNamespaceUuids,
    selectionError,
    selectionWarning
  } = props;
  const [namespaceSearchTerm, setNamespaceSearchTerm] = useState('');
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof NamespaceItem>('name');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);
  const [isTooltipOpen, setIsTooltipOpen] = useState<boolean>(false);
  const [isMouseOverTooltip, setIsMouseOverTooltip] = useState<boolean>(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();

  const sourceUniverseNamespaceQuery = useQuery<UniverseNamespace[]>(
    universeQueryKey.namespaces(sourceUniverseUUID),
    () => api.fetchUniverseNamespaces(sourceUniverseUUID)
  );

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(sourceUniverseUUID, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(sourceUniverseUUID, XCLUSTER_UNIVERSE_TABLE_FILTERS).then(
        (response) => response.data
      )
  );
  const targetUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(targetUniverseUUID, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(targetUniverseUUID, XCLUSTER_UNIVERSE_TABLE_FILTERS).then(
        (response) => response.data
      )
  );

  const sourceUniverseQuery = useQuery<Universe>(universeQueryKey.detail(sourceUniverseUUID), () =>
    api.fetchUniverse(sourceUniverseUUID)
  );
  const targetUniverseQuery = useQuery<Universe>(universeQueryKey.detail(targetUniverseUUID), () =>
    api.fetchUniverse(targetUniverseUUID)
  );

  const sharedXClusterConfigUUIDs =
    sourceUniverseQuery?.data && targetUniverseQuery?.data
      ? getSharedXClusterConfigs(sourceUniverseQuery.data, targetUniverseQuery.data)
      : [];
  /**
   * Queries for shared xCluster config UUIDs
   */
  const sharedXClusterConfigQueries = useQueries(
    sharedXClusterConfigUUIDs.map((xClusterConfigUUID) => ({
      queryKey: xClusterQueryKey.detail(xClusterConfigUUID),
      queryFn: () => fetchXClusterConfig(xClusterConfigUUID)
    }))
    // The unsafe cast is needed due to an issue with useQueries typing
    // Upgrading react-query to v3.28 may solve this issue: https://github.com/TanStack/query/issues/1675
  ) as UseQueryResult<XClusterConfig>[];

  const globalRuntimeConfigQuery = useQuery(runtimeConfigQueryKey.globalScope(), () =>
    api.fetchRuntimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE, true)
  );

  if (
    sourceUniverseNamespaceQuery.isLoading ||
    sourceUniverseNamespaceQuery.isIdle ||
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle ||
    targetUniverseTablesQuery.isLoading ||
    targetUniverseTablesQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    targetUniverseQuery.isLoading ||
    targetUniverseQuery.isIdle ||
    globalRuntimeConfigQuery.isLoading ||
    globalRuntimeConfigQuery.isIdle
  ) {
    return <YBLoading />;
  }

  if (
    sourceUniverseNamespaceQuery.isError ||
    sourceUniverseTablesQuery.isError ||
    sourceUniverseQuery.isError
  ) {
    const sourceUniverseLabel = isDrInterface ? 'DR primary universe' : 'source universe';
    return (
      <YBErrorIndicator customErrorMessage={`Error fetching ${sourceUniverseLabel} information.`} />
    );
  }
  if (targetUniverseTablesQuery.isError || targetUniverseQuery.isError) {
    const targetUniverseLabel = isDrInterface ? 'DR replica universe' : 'source universe';
    return (
      <YBErrorIndicator customErrorMessage={`Error fetching ${targetUniverseLabel} information.`} />
    );
  }
  if (globalRuntimeConfigQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching runtime configurations." />;
  }

  const toggleTableGroup = (
    isSelected: boolean,
    tableReplicationCandidates: TableReplicationCandidate[]
  ) => {
    if (isSelected) {
      const currentSelectedTableUuids = new Set(selectedTableUUIDs);

      tableReplicationCandidates.forEach((tableReplicationCandidate) => {
        currentSelectedTableUuids.add(getTableUuid(tableReplicationCandidate));

        if (tableReplicationCandidate.isIndexTable && tableReplicationCandidate.mainTableUUID) {
          currentSelectedTableUuids.add(
            formatUuidForXCluster(tableReplicationCandidate.mainTableUUID)
          );
        }
        tableReplicationCandidate.indexTableIDs?.forEach((indexTableId) =>
          currentSelectedTableUuids.add(indexTableId)
        );
      });
      setSelectedTableUUIDs(Array.from(currentSelectedTableUuids));
    } else {
      const removedTableUuids = new Set();
      tableReplicationCandidates.forEach((tableReplicationCandidate) => {
        removedTableUuids.add(getTableUuid(tableReplicationCandidate));
        tableReplicationCandidate.indexTableIDs?.forEach((indexTableId) =>
          removedTableUuids.add(indexTableId)
        );
      });
      setSelectedTableUUIDs(
        selectedTableUUIDs.filter((tableUUID) => !removedTableUuids.has(tableUUID))
      );
    }
  };

  const handleTableGroupToggle = (isSelected: boolean, rows: TableReplicationCandidate[]) => {
    toggleTableGroup(isSelected, rows);
    return true;
  };

  const handleTableToggle = (
    tableReplicationCandidate: TableReplicationCandidate,
    isSelected: boolean
  ) => {
    toggleTableGroup(isSelected, [tableReplicationCandidate]);
  };

  const toggleNamespaceGroup = (isSelected: boolean, namespaceItems: NamespaceItem[]) => {
    if (isSelected) {
      const currentSelectedNamespaces = new Set(selectedNamespaceUuids);

      namespaceItems.forEach((namespaceItem) => {
        currentSelectedNamespaces.add(namespaceItem.uuid);
      });
      setSelectedNamespaceUuids(Array.from(currentSelectedNamespaces));
    } else {
      const removedNamespaceUuids = new Set(
        namespaceItems.map((namespaceItem) => namespaceItem.uuid)
      );

      setSelectedNamespaceUuids(
        selectedNamespaceUuids.filter(
          (namespaceUuid: string) => !removedNamespaceUuids.has(namespaceUuid)
        )
      );
    }
  };

  const handleNamespaceGroupToggle = (isSelected: boolean, namespaceItems: NamespaceItem[]) => {
    const selectableTables = namespaceItems.reduce(
      (table: MainTableReplicationCandidate[], namespaceItem) => {
        return table.concat(
          namespaceItem.tables.filter((table) => isTableToggleable(table, props.configAction))
        );
      },
      []
    );

    toggleNamespaceGroup(isSelected, namespaceItems);
    toggleTableGroup(isSelected, selectableTables);
    return true;
  };

  const handleNamespaceToggle = (namespaceItem: NamespaceItem, isSelected: boolean) => {
    toggleNamespaceGroup(isSelected, [namespaceItem]);
    toggleTableGroup(
      isSelected,
      namespaceItem.tables.filter((table) => isTableToggleable(table, props.configAction))
    );
  };

  // Casting workaround: https://github.com/JedWatson/react-select/issues/2902
  const handleTableTypeChange = (option: ValueType<typeof TABLE_TYPE_OPTIONS[number]>) => {
    if (!isFixedTableType) {
      setTableType((option as typeof TABLE_TYPE_OPTIONS[number])?.value);

      // Clear current item selection.
      // Form submission should only contain tables of the same type (YSQL or YCQL).
      setSelectedNamespaceUuids([]);
      setSelectedTableUUIDs([]);
    }
  };

  const sharedXClusterConfigs: XClusterConfig[] = [];
  for (const xClusterConfigQuery of sharedXClusterConfigQueries) {
    if (xClusterConfigQuery.isLoading || xClusterConfigQuery.isIdle) {
      return <YBLoading />;
    }
    if (xClusterConfigQuery.isError) {
      return <YBErrorIndicator />;
    }
    sharedXClusterConfigs.push(xClusterConfigQuery.data);
  }

  const replicationItems =
    props.configAction === XClusterConfigAction.ADD_TABLE ||
    props.configAction === XClusterConfigAction.MANAGE_TABLE
      ? getReplicationItemsFromTables(
          sourceUniverseNamespaceQuery.data,
          sourceUniverseTablesQuery.data,
          sharedXClusterConfigs,
          props.xClusterConfigUUID
        )
      : getReplicationItemsFromTables(
          sourceUniverseNamespaceQuery.data,
          sourceUniverseTablesQuery.data,
          sharedXClusterConfigs
        );
  const namespaceItems = Object.entries(replicationItems[tableType].namespaces)
    .filter(([_, namespaceItem]) => hasSubstringMatch(namespaceItem.name, namespaceSearchTerm))
    .map(([_, namespaceItem]) => namespaceItem);
  const untoggleableNamespaceUuids = getUntoggleableNamespaceUuids(
    props.configAction,
    namespaceItems,
    initialNamespaceUuids,
    selectedNamespaceUuids,
    tableType
  );
  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type NamespaceItem.
      setSortField(sortName as keyof NamespaceItem);
      setSortOrder(sortOrder);
    }
  };

  const tableDescriptor = isDrInterface
    ? t('selectionDescriptorDr')
    : t('selectionDescriptorXCluster');
  const runtimeConfigEntries = globalRuntimeConfigQuery.data.configEntries ?? [];
  const isTransactionalAtomicityEnabled = runtimeConfigEntries.some(
    (config: any) =>
      config.key === RuntimeConfigKey.XCLUSTER_TRANSACTIONAL_ATOMICITY_FEATURE_FLAG &&
      config.value === 'true'
  );
  const ybSoftwareVersion = getPrimaryCluster(sourceUniverseQuery.data.universeDetails.clusters)
    ?.userIntent.ybSoftwareVersion;

  const participantsHaveLinkedXClusterConfig = hasLinkedXClusterConfig([
    sourceUniverseQuery.data,
    targetUniverseQuery.data
  ]);
  const isTransactionalAtomicitySupported =
    !!ybSoftwareVersion &&
    compareYBSoftwareVersions({
      versionA: TRANSACTIONAL_ATOMICITY_YB_SOFTWARE_VERSION_THRESHOLD,
      versionB: ybSoftwareVersion,
      options: {
        suppressFormatError: true
      }
    }
    ) < 0 &&
    !participantsHaveLinkedXClusterConfig;
  return (
    <>
      {isTransactionalAtomicityEnabled &&
        !isDrInterface &&
        props.configAction === XClusterConfigAction.CREATE &&
        tableType === TableType.PGSQL_TABLE_TYPE && (
          <Box display="flex" gridGap="5px">
            <YBCheckBox
              checkState={props.isTransactionalConfig}
              onClick={props.handleTransactionalConfigCheckboxClick}
              label="Enable transactional atomicity"
              disabled={!isTransactionalAtomicitySupported}
            />
            {/* This tooltip needs to be have a z-index greater than the z-index on the modal (3100)*/}
            <YBTooltip
              open={isTooltipOpen || isMouseOverTooltip}
              title={
                <p>
                  YBA support for transactional atomicity has the following constraints:
                  <ol>
                    <li>
                      The minimum YBDB version that supports transactional atomicity is 2.18.1.0-b1.
                    </li>
                    <li>PITR must be enabled on the target universe.</li>
                    <li>
                      Neither the source universe nor the target universe is a participant in any
                      other xCluster configuration.
                    </li>
                  </ol>
                  You may find further information on this feature on our{' '}
                  <a href={XCLUSTER_REPLICATION_DOCUMENTATION_URL}>public docs.</a>
                </p>
              }
              PopperProps={{ style: { zIndex: 4000, pointerEvents: 'auto' } }}
              style={{ marginBottom: '5px' }}
            >
              <img
                className={styles.transactionalSupportTooltip}
                alt="Info"
                src={InfoMessageIcon}
                onClick={() => setIsTooltipOpen(!isTooltipOpen)}
                onMouseOver={() => setIsMouseOverTooltip(true)}
                onMouseOut={() => setIsMouseOverTooltip(false)}
              />
            </YBTooltip>
          </Box>
        )}
      <div className={styles.tableDescriptor}>{tableDescriptor}</div>
      {!isDrInterface && (
        <div className={styles.tableToolbar}>
          <Select
            styles={TABLE_TYPE_SELECT_STYLES}
            options={TABLE_TYPE_OPTIONS}
            onChange={handleTableTypeChange}
            value={{ value: tableType, label: TableTypeLabel[tableType] }}
            isDisabled={isFixedTableType}
          />
          <YBInputField
            containerClassName={styles.namespaceSearchInput}
            placeHolder="Search for databases.."
            onValueChanged={(searchTerm: string) => setNamespaceSearchTerm(searchTerm)}
          />
        </div>
      )}
      <div className={styles.bootstrapTableContainer}>
        <BootstrapTable
          tableContainerClass={styles.bootstrapTable}
          maxHeight="450px"
          data={namespaceItems
            .sort((a, b) => tableSort<NamespaceItem>(a, b, sortField, sortOrder, 'uuid'))
            .slice((activePage - 1) * pageSize, activePage * pageSize)}
          expandableRow={(namespaceItem: NamespaceItem) => {
            return namespaceItem.tables.length > 0;
          }}
          expandComponent={(namespaceItem: NamespaceItem) => (
            <ExpandedTableSelect
              row={namespaceItem}
              isSelectable={
                tableType === TableType.YQL_TABLE_TYPE ||
                (props.configAction === XClusterConfigAction.MANAGE_TABLE &&
                  initialNamespaceUuids.includes(namespaceItem.uuid) &&
                  selectedNamespaceUuids.includes(namespaceItem.uuid))
              }
              selectedTableUUIDs={selectedTableUUIDs}
              tableType={tableType}
              xClusterConfigAction={props.configAction}
              handleTableSelect={handleTableToggle}
              handleTableGroupSelect={handleTableGroupToggle}
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
            onSelect: handleNamespaceToggle,
            onSelectAll: handleNamespaceGroupToggle,
            selected: selectedNamespaceUuids,
            unselectable: untoggleableNamespaceUuids
          }}
          options={tableOptions}
        >
          <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
          <TableHeaderColumn dataField="name" dataSort={true}>
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
      {namespaceItems.length > TABLE_MIN_PAGE_SIZE && (
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
            className={styles.ybPagination}
            numPages={Math.ceil(namespaceItems.length / pageSize)}
            onChange={(newPageNum: number) => {
              setActivePage(newPageNum);
            }}
            activePage={activePage}
          />
        </div>
      )}
      {tableType === TableType.YQL_TABLE_TYPE ||
      props.configAction === XClusterConfigAction.MANAGE_TABLE ? (
        <Typography variant="body2">
          {t('tableSelectionCount', {
            selectedTableCount: selectedTableUUIDs.length,
            availableTableCount: replicationItems[tableType].tableCount
          })}
        </Typography>
      ) : (
        <Typography variant="body2">
          {t('databaseSelectionCount', {
            selectedDatabaseCount: selectedNamespaceUuids.length,
            availableDatabaseCount: Object.keys(replicationItems.PGSQL_TABLE_TYPE.namespaces).length
          })}
        </Typography>
      )}
      {(selectionError || selectionWarning) && (
        <div className={styles.validationContainer}>
          {selectionError && (
            <YBBanner variant={YBBannerVariant.DANGER}>
              <Typography variant="body1">{selectionError.title}</Typography>
              <Typography variant="body2" component="p">
                {selectionError.body}
              </Typography>
            </YBBanner>
          )}
          {selectionWarning && (
            <YBBanner variant={YBBannerVariant.WARNING}>
              <Typography variant="body1">{selectionWarning.title}</Typography>
              <Typography variant="body2" component="p">
                {selectionWarning.body}
              </Typography>
            </YBBanner>
          )}
        </div>
      )}
      <Box display="flex" flexDirection="column" marginTop={2} gridGap={theme.spacing(2)}>
        <YBBanner variant={YBBannerVariant.INFO}>
          <Typography variant="body2">
            <Trans
              i18nKey={`${TRANSLATION_KEY_PREFIX}.${
                props.isDrInterface ? 'matchingTableNoteDr' : 'matchingTableNoteXCluster'
              }`}
              components={{ bold: <b /> }}
            />
          </Typography>
        </YBBanner>
        {!isDrInterface && (
          <CollapsibleNote noteContent={NOTE_CONTENT} expandContent={NOTE_EXPAND_CONTENT} />
        )}
      </Box>
    </>
  );
};

/**
 * Group tables by {@link TableType} and then by namespace name.
 */
const getReplicationItemsFromTables = (
  sourceUniverseNamespaces: UniverseNamespace[],
  sourceUniverseTables: YBTable[],
  sharedXClusterConfigs: XClusterConfig[],
  currentXClusterConfigUUID?: string
): ReplicationItems => {
  const namespaceNameToNamespaceUuid = Object.fromEntries(
    sourceUniverseNamespaces.map((namespace) => [namespace.name, namespace.namespaceUUID])
  );
  const tableUuidToTable = Object.fromEntries(
    sourceUniverseTables.map((table) => [getTableUuid(table), table])
  );

  return sourceUniverseTables.reduce(
    (items: ReplicationItems, sourceTable) => {
      // Filter out index tables because we will add them as a field under the main table.
      if (
        sourceTable.relationType !== YBTableRelationType.INDEX_TABLE_RELATION &&
        (XCLUSTER_SUPPORTED_TABLE_TYPES as readonly TableType[]).includes(sourceTable.tableType)
      ) {
        const mainTableReplicationEligibility = getXClusterTableEligibilityDetails(
          sourceTable,
          sharedXClusterConfigs,
          currentXClusterConfigUUID
        );

        // Add information about associated index tables if applicable.
        const indexTables = [] as IndexTableReplicationCandidate[];
        let indexTablesTotalSize = 0;
        sourceTable.indexTableIDs?.forEach((indexTableId) => {
          const indexTable = tableUuidToTable[indexTableId];
          const indexTableReplicationEligibility = getXClusterTableEligibilityDetails(
            indexTable,
            sharedXClusterConfigs,
            currentXClusterConfigUUID
          );
          const indexTableReplicationCandidate = {
            ...indexTable,
            eligibilityDetails: indexTableReplicationEligibility,
            tableUUID: getTableUuid(indexTable)
          };
          indexTables.push(indexTableReplicationCandidate);
          indexTablesTotalSize += indexTable.sizeBytes;
        });

        const mainTableReplicationCandidate: MainTableReplicationCandidate = {
          ...sourceTable,
          eligibilityDetails: mainTableReplicationEligibility,
          tableUUID: getTableUuid(sourceTable),
          indexTables: indexTables
        };
        const {
          tableType,
          keySpace: namespace,
          sizeBytes,
          eligibilityDetails
        } = mainTableReplicationCandidate;
        const namespaceUuid = namespaceNameToNamespaceUuid[namespace] ?? namespace;

        items[tableType].namespaces[namespaceUuid] = items[tableType].namespaces[namespaceUuid] ?? {
          uuid: namespaceUuid,
          name: namespace,
          tableEligibilityCount: {
            ineligible: 0,
            eligibleInCurrentConfig: 0
          },
          sizeBytes: 0,
          tables: []
        };

        // Push table and update metadata
        items[tableType].namespaces[namespaceUuid].tables.push(mainTableReplicationCandidate);
        items[tableType].namespaces[namespaceUuid].sizeBytes += sizeBytes + indexTablesTotalSize;
        items[tableType].tableCount += 1 + (mainTableReplicationCandidate.indexTables?.length ?? 0);
        if (XCLUSTER_TABLE_INELIGIBLE_STATUSES.includes(eligibilityDetails.status)) {
          items[tableType].namespaces[namespaceUuid].tableEligibilityCount.ineligible += 1;
        } else if (
          eligibilityDetails.status === XClusterTableEligibility.ELIGIBLE_IN_CURRENT_CONFIG
        ) {
          items[tableType].namespaces[
            namespaceUuid
          ].tableEligibilityCount.eligibleInCurrentConfig += 1;
        }
      }
      return items;
    },
    {
      [TableType.PGSQL_TABLE_TYPE]: {
        namespaces: {},
        tableCount: 0
      },
      [TableType.YQL_TABLE_TYPE]: {
        namespaces: {},
        tableCount: 0
      }
    }
  );
};

/**
 * A table is eligible for replication if table is NOT part of another existing
 * xCluster config between the same universes in the same direction.
 */
const getXClusterTableEligibilityDetails = (
  sourceTable: YBTable,
  sharedXClusterConfigs: XClusterConfig[],
  currentXClusterConfigUUID?: string
): EligibilityDetails => {
  for (const xClusterConfig of sharedXClusterConfigs) {
    const xClusterConfigTables = new Set(xClusterConfig.tables);
    if (xClusterConfigTables.has(sourceTable.tableID)) {
      return {
        status:
          xClusterConfig.uuid === currentXClusterConfigUUID
            ? XClusterTableEligibility.ELIGIBLE_IN_CURRENT_CONFIG
            : XClusterTableEligibility.INELIGIBLE_IN_USE,
        xClusterConfigName: xClusterConfig.name
      };
    }
  }

  return { status: XClusterTableEligibility.ELIGIBLE_UNUSED };
};

/**
 * Return the list of namespace UUIDs which are untoggleable.
 *
 * Add Table
 * - YSQL namespaces are untoggleable if they contain at least one ineligible table or
 *   no unused eligible table.
 * - YCQL namespaces are untoggleable if they contain no unused eligible table.
 *
 * Edit Table
 * - Always allow removing namespaces.
 * - Allow selecing namespaces which do not contain ineligible tables.
 */
const getUntoggleableNamespaceUuids = (
  xClusterConfigAction: XClusterConfigAction,
  namespaceItems: NamespaceItem[],
  initialNamespaces: string[],
  selectedNamespaces: string[],
  tableType: XClusterTableType
): string[] =>
  namespaceItems
    .filter((namespaceItem) => {
      if (xClusterConfigAction === XClusterConfigAction.MANAGE_TABLE) {
        // We want to allow removing a namespace with ineligible tables
        // if they got into that state some how.
        return !(
          (initialNamespaces.includes(namespaceItem.uuid) &&
            selectedNamespaces.includes(namespaceItem.uuid)) ||
          namespaceItem.tableEligibilityCount.ineligible === 0
        );
      }
      switch (tableType) {
        case TableType.PGSQL_TABLE_TYPE:
          return (
            namespaceItem.tableEligibilityCount.ineligible > 0 ||
            namespaceItem.tableEligibilityCount.eligibleInCurrentConfig ===
              namespaceItem.tables.length
          );
        case TableType.YQL_TABLE_TYPE:
          return (
            namespaceItem.tableEligibilityCount.ineligible +
              namespaceItem.tableEligibilityCount.eligibleInCurrentConfig ===
            namespaceItem.tables.length
          );
        default:
          return assertUnreachableCase(tableType);
      }
    })
    .map((namespaceItem) => namespaceItem.uuid);
