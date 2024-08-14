import { useState } from 'react';
import {
  BootstrapTable,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';
import { useQueries, useQuery, UseQueryResult } from 'react-query';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';

import {
  fetchTablesInUniverse,
  fetchXClusterConfig
} from '../../../../actions/xClusterReplication';
import { api, universeQueryKey, xClusterQueryKey } from '../../../../redesign/helpers/api';
import { YBControlledSelect } from '../../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import {
  formatBytes,
  getSharedXClusterConfigs,
  tableSort,
  isTableToggleable,
  formatUuidForXCluster,
  getNamespaceIdentifierToNamespaceUuidMap,
  getNamespaceIdentifier,
  getInConfigTableUuidsToTableDetailsMap
} from '../../ReplicationUtils';
import {
  XClusterConfigAction,
  XCLUSTER_TABLE_INELIGIBLE_STATUSES,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../../constants';
import YBPagination from '../../../tables/YBPagination/YBPagination';
import { CollapsibleNote } from '../CollapsibleNote';
import { ExpandedTableSelect } from './ExpandedTableSelect';
import { XClusterTableEligibility } from '../../constants';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { SortOrder, YBTableRelationType } from '../../../../redesign/helpers/constants';
import { ExpandColumnComponent } from './ExpandColumnComponent';
import { getTableUuid } from '../../../../utils/tableUtils';
import { YBBanner, YBBannerVariant } from '../../../common/descriptors';
import {
  SearchToken,
  YBSmartSearchBar
} from '../../../../redesign/components/YBSmartSearchBar/YBSmartSearchBar';
import { YBButton } from '../../../../redesign/components';
import {
  FieldType,
  isMatchedBySearchToken
} from '../../../../redesign/components/YBSmartSearchBar/helpers';

import { TableType, Universe, UniverseNamespace, YBTable } from '../../../../redesign/helpers/dtos';
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
  sourceUniverseUuid: string;
  targetUniverseUuid: string;
  selectedTableUuids: string[];
  setSelectedTableUuids: (tableUuids: string[]) => void;
  isDrInterface: boolean;
  tableType: XClusterTableType;
  initialNamespaceUuids: string[];
  selectedNamespaceUuids: string[];
  setSelectedNamespaceUuids: (selectedNamespaceUuids: string[]) => void;
  selectionError: { title?: string; body?: string } | null;
  selectionWarning: { title: string; body: string } | null;
  isTransactionalConfig: boolean;
}

export type TableSelectProps =
  | (CommonTableSelectProps & {
      configAction: typeof XClusterConfigAction.CREATE;
    })
  | (CommonTableSelectProps & {
      configAction: typeof XClusterConfigAction.MANAGE_TABLE;
      xClusterConfigUuid: string;
      unreplicatedTableInReplicatedNamespace: Set<string>;
      tableUuidsDroppedOnSource: Set<string>;
      tableUuidsDroppedOnTarget: Set<string>;
    });

const useStyles = makeStyles(() => ({
  selectUnreplicatedTablesButton: {
    height: '40px'
  }
}));

const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40] as const;

const NOTE_CONTENT = (
  <div>
    <p>
      <b>Note! </b>
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
    sourceUniverseUuid,
    targetUniverseUuid,
    selectedTableUuids,
    setSelectedTableUuids,
    tableType,
    isDrInterface,
    initialNamespaceUuids,
    selectedNamespaceUuids,
    setSelectedNamespaceUuids,
    selectionError,
    selectionWarning,
    isTransactionalConfig
  } = props;
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof NamespaceItem>('name');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);
  const [searchTokens, setSearchTokens] = useState<SearchToken[]>([]);

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  const sourceUniverseNamespaceQuery = useQuery<UniverseNamespace[]>(
    universeQueryKey.namespaces(sourceUniverseUuid),
    () => api.fetchUniverseNamespaces(sourceUniverseUuid)
  );

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(sourceUniverseUuid, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(sourceUniverseUuid, XCLUSTER_UNIVERSE_TABLE_FILTERS).then(
        (response) => response.data
      )
  );
  const targetUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(targetUniverseUuid, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(targetUniverseUuid, XCLUSTER_UNIVERSE_TABLE_FILTERS).then(
        (response) => response.data
      )
  );
  const sourceUniverseQuery = useQuery<Universe>(universeQueryKey.detail(sourceUniverseUuid), () =>
    api.fetchUniverse(sourceUniverseUuid)
  );
  const targetUniverseQuery = useQuery<Universe>(universeQueryKey.detail(targetUniverseUuid), () =>
    api.fetchUniverse(targetUniverseUuid)
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
    targetUniverseQuery.isIdle
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

  const toggleTableGroup = (
    isSelected: boolean,
    tableReplicationCandidates: TableReplicationCandidate[]
  ) => {
    if (isSelected) {
      const currentSelectedTableUuids = new Set(selectedTableUuids);

      tableReplicationCandidates.forEach((tableReplicationCandidate) => {
        currentSelectedTableUuids.add(getTableUuid(tableReplicationCandidate));

        // When adding an index table, also add the main table.
        // Users are unable to toggle index tables for txn config and YCQL configs
        if (tableReplicationCandidate.isIndexTable && tableReplicationCandidate.mainTableUUID) {
          currentSelectedTableUuids.add(
            formatUuidForXCluster(tableReplicationCandidate.mainTableUUID)
          );
        }

        // When adding a main table, also add the index tables.
        tableReplicationCandidate.indexTableIDs?.forEach((indexTableId) =>
          currentSelectedTableUuids.add(indexTableId)
        );
      });
      setSelectedTableUuids(Array.from(currentSelectedTableUuids));
    } else {
      const removedTableUuids = new Set();
      tableReplicationCandidates.forEach((tableReplicationCandidate) => {
        removedTableUuids.add(getTableUuid(tableReplicationCandidate));

        // When removing a main table, also remove the index tables.
        tableReplicationCandidate.indexTableIDs?.forEach((indexTableId) =>
          removedTableUuids.add(indexTableId)
        );
      });
      setSelectedTableUuids(
        selectedTableUuids.filter((tableUUID) => !removedTableUuids.has(tableUUID))
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
      const currentSelectedNamespacesUuids = new Set(selectedNamespaceUuids);

      namespaceItems.forEach((namespaceItem) => {
        currentSelectedNamespacesUuids.add(namespaceItem.uuid);
      });
      setSelectedNamespaceUuids(Array.from(currentSelectedNamespacesUuids));
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

  // For namespace toggles, we toggle tables from `allTables` instead of `tables` because namespace selection
  // means the user wants all tables from that namespace to be selected/deselected regardless of the search token match.
  // If the user wants to toggle only the matching tables under a particular namespace, there are selection checkboxes
  // in the table selection sub-table for that.
  const handleNamespaceGroupToggle = (isSelected: boolean, namespaceItems: NamespaceItem[]) => {
    const selectableTables = namespaceItems.reduce(
      (table: MainTableReplicationCandidate[], namespaceItem) => {
        return table.concat(
          namespaceItem.allTables.filter((table) => isTableToggleable(table, props.configAction))
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
      namespaceItem.allTables.filter((table) => isTableToggleable(table, props.configAction))
    );
  };

  const handleSearchTokenChange = (searchTokens: SearchToken[]) => {
    setSearchTokens(searchTokens);
    setActivePage(1);
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

  // Deselect any tables which are dropped on the source universe.
  if (props.configAction === XClusterConfigAction.MANAGE_TABLE) {
    if (selectedTableUuids.some((tableUuid) => props.tableUuidsDroppedOnSource.has(tableUuid))) {
      setSelectedTableUuids(
        selectedTableUuids.filter((tableUUID) => !props.tableUuidsDroppedOnSource.has(tableUUID))
      );
    }
  }
  const { replicationItems, namespaceUuidToUnreplicatedTableCandidates } =
    props.configAction === XClusterConfigAction.MANAGE_TABLE
      ? getReplicationItemsFromTables(
          sourceUniverseNamespaceQuery.data,
          sourceUniverseTablesQuery.data,
          sharedXClusterConfigs,
          searchTokens,
          isTransactionalConfig,
          tableType,
          props.xClusterConfigUuid,
          props.unreplicatedTableInReplicatedNamespace,
          props.tableUuidsDroppedOnTarget
        )
      : getReplicationItemsFromTables(
          sourceUniverseNamespaceQuery.data,
          sourceUniverseTablesQuery.data,
          sharedXClusterConfigs,
          searchTokens,
          isTransactionalConfig,
          tableType
        );

  const selectAllUnreplicatedTablesInReplicatedNamespaces = () => {
    const tableCandidates: TableReplicationCandidate[] = [];

    // Since the user may deselect a namespace in the select table modal,
    // we need to ensure we're adding only the unreplicated tables which
    // belong to currently selected namespaces.
    // Otherwise, we would be adding a proper subset of tables in a namespace
    // which is not supported for YSQL due to backup & restore being done at the
    // namespace (database) level.
    Object.entries(namespaceUuidToUnreplicatedTableCandidates).forEach(
      ([namespaceId, unreplicatedTableCandidates]) => {
        if (selectedNamespaceUuids.includes(namespaceId)) {
          tableCandidates.push(...unreplicatedTableCandidates);
        }
      }
    );

    // We run toggleTableGroup only once with an array containing all tableCandidates across
    // all selected namespaces to avoid a race condition resulting from shared
    // selectedTableUuids access & modification.
    toggleTableGroup(true, tableCandidates);
  };

  const namespaceItems = Object.entries(replicationItems.namespaces).map(
    ([_, namespaceItem]) => namespaceItem
  );
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

  const selectedSearchMatchingTableCount: number = selectedTableUuids.filter((tableUuid) =>
    replicationItems.searchMatchingTableUuids.has(tableUuid)
  ).length;
  const selectedSearchMatchingNamespaceCount: number = selectedNamespaceUuids.filter(
    (namespaceUuid) => replicationItems.searchMatchingNamespaceUuids.has(namespaceUuid)
  ).length;
  const tableDescriptor = isDrInterface
    ? t('selectionDescriptorDr')
    : t('selectionDescriptorXCluster');
  return (
    <>
      <div className={styles.tableDescriptor}>{tableDescriptor}</div>
      <Box display="flex" width="100%" gridGap={theme.spacing(1)}>
        <YBSmartSearchBar
          searchTokens={searchTokens}
          onSearchTokensChange={handleSearchTokenChange}
          recognizedModifiers={['database', 'table', 'sizeBytes', 'unreplicatedTable']}
          placeholder={t('tablesSearchBarPlaceholder')}
        />
        {props.configAction === XClusterConfigAction.MANAGE_TABLE &&
          tableType === TableType.PGSQL_TABLE_TYPE && (
            <YBButton
              variant="primary"
              size="medium"
              type="button"
              classes={{ root: classes.selectUnreplicatedTablesButton }}
              onClick={selectAllUnreplicatedTablesInReplicatedNamespaces}
            >
              {t('selectUnreplicatedTablesButton')}
            </YBButton>
          )}
      </Box>
      <div className={styles.bootstrapTableContainer}>
        <BootstrapTable
          tableContainerClass={styles.bootstrapTable}
          maxHeight="450px"
          data={namespaceItems
            // Hide namespaces which don't have a matching table
            .filter((namespaceItem) => namespaceItem.tables.length > 0)
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
              isTransactionalConfig={isTransactionalConfig}
              selectedTableUUIDs={selectedTableUuids}
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
            selectedTableCount: selectedSearchMatchingTableCount,
            availableTableCount: replicationItems.searchMatchingTableUuids.size
          })}
        </Typography>
      ) : (
        <Typography variant="body2">
          {t('databaseSelectionCount', {
            selectedDatabaseCount: selectedSearchMatchingNamespaceCount,
            availableDatabaseCount: replicationItems.searchMatchingNamespaceUuids.size
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
        {props.configAction !== XClusterConfigAction.CREATE &&
          props.tableUuidsDroppedOnSource.size > 0 && (
            <YBBanner variant={YBBannerVariant.INFO}>
              <Typography variant="body2">
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.${
                    props.isDrInterface
                      ? 'droppedTablesInfoTextDr'
                      : 'droppedTablesInfoTextXCluster'
                  }`}
                  values={{ droppedTableCount: props.tableUuidsDroppedOnSource.size }}
                  components={{ bold: <b /> }}
                />
              </Typography>
            </YBBanner>
          )}
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
  searchTokens: SearchToken[],
  isTransactionalConfig: boolean,
  tableType: XClusterTableType,
  currentXClusterConfigUUID?: string,
  unreplicatedTableInReplicatedNamespace?: Set<string>,
  tableUuidsDroppedOnTarget?: Set<string>
): {
  replicationItems: ReplicationItems;
  namespaceUuidToUnreplicatedTableCandidates: {
    [namespace: string]: TableReplicationCandidate[];
  };
} => {
  const namespaceIdentifierToNamespaceUuid = getNamespaceIdentifierToNamespaceUuidMap(
    sourceUniverseNamespaces
  );
  const tableUuidToTable = Object.fromEntries(
    sourceUniverseTables.map((table) => [getTableUuid(table), table])
  );
  const namespaceUuidToUnreplicatedTableCandidates: {
    [namespace: string]: TableReplicationCandidate[];
  } = {};
  const replicationItems = sourceUniverseTables.reduce(
    (items: ReplicationItems, sourceTable) => {
      // Filter out index tables because we will add them as a field under the main table.
      if (
        sourceTable.relationType !== YBTableRelationType.INDEX_TABLE_RELATION &&
        sourceTable.tableType === tableType
      ) {
        const mainTableReplicationEligibility = getXClusterTableEligibilityDetails(
          sourceTable,
          sharedXClusterConfigs,
          currentXClusterConfigUUID
        );

        // Add associated index tables if the current source table is a main table.
        const indexTables = [] as IndexTableReplicationCandidate[];
        const unreplicatedIndexTablesInReplicatedNamespace: IndexTableReplicationCandidate[] = [];
        let indexTablesTotalSize = 0;
        sourceTable.indexTableIDs?.forEach((indexTableUuid) => {
          const indexTable = tableUuidToTable[indexTableUuid];
          const indexTableReplicationEligibility = getXClusterTableEligibilityDetails(
            indexTable,
            sharedXClusterConfigs,
            currentXClusterConfigUUID
          );
          const isUnreplicatedTableInReplicatedNamespace = !!unreplicatedTableInReplicatedNamespace?.has(
            indexTableUuid
          );
          const isDroppedOnTarget = !!tableUuidsDroppedOnTarget?.has(indexTableUuid);
          const indexTableReplicationCandidate: IndexTableReplicationCandidate = {
            ...indexTable,
            eligibilityDetails: indexTableReplicationEligibility,
            tableUUID: indexTableUuid,
            isUnreplicatedTableInReplicatedNamespace,
            isDroppedOnTarget
          };

          // The client only needs to select and submit index tables when dealing with non-txn xCluster configs.
          // For txn xCluster configs, the index table is added/dropped automatically after performing the ddl operation
          // on the source/target databases and reconciling with YBA.
          if (
            !isTransactionalConfig &&
            isUnreplicatedTableInReplicatedNamespace &&
            indexTableReplicationEligibility
          ) {
            unreplicatedIndexTablesInReplicatedNamespace.push(indexTableReplicationCandidate);
          }

          indexTables.push(indexTableReplicationCandidate);
          indexTablesTotalSize += indexTable.sizeBytes;
        });

        const mainTableUuid = getTableUuid(sourceTable);
        const isUnreplicatedTableInReplicatedNamespace = !!unreplicatedTableInReplicatedNamespace?.has(
          mainTableUuid
        );
        const isDroppedOnTarget = !!tableUuidsDroppedOnTarget?.has(mainTableUuid);
        const mainTableReplicationCandidate: MainTableReplicationCandidate = {
          ...sourceTable,
          eligibilityDetails: mainTableReplicationEligibility,
          tableUUID: mainTableUuid,
          indexTables: indexTables,
          isUnreplicatedTableInReplicatedNamespace,
          isDroppedOnTarget
        };
        const {
          keySpace: namespaceName,
          sizeBytes,
          eligibilityDetails
        } = mainTableReplicationCandidate;
        const namespaceId =
          namespaceIdentifierToNamespaceUuid[getNamespaceIdentifier(namespaceName, tableType)] ??
          namespaceName;

        // Store list of unreplicated tables at the top level to make it easy to reference when selecting
        // all unreplicated tables in replicated namespaces.
        // We group the tables by namespaces because we want to select only the unreplicated tables in
        // currently selected namespaces. Replicated namespaces may be deselected as part of the same
        // request.
        if (isUnreplicatedTableInReplicatedNamespace && mainTableReplicationEligibility) {
          namespaceUuidToUnreplicatedTableCandidates[namespaceId] =
            namespaceUuidToUnreplicatedTableCandidates[namespaceId] ?? [];
          namespaceUuidToUnreplicatedTableCandidates[namespaceId].push(
            mainTableReplicationCandidate
          );
        }
        if (unreplicatedIndexTablesInReplicatedNamespace.length) {
          namespaceUuidToUnreplicatedTableCandidates[namespaceId] =
            namespaceUuidToUnreplicatedTableCandidates[namespaceId] ?? [];
          namespaceUuidToUnreplicatedTableCandidates[namespaceId].push(
            ...unreplicatedIndexTablesInReplicatedNamespace
          );
        }

        items.namespaces[namespaceId] = items.namespaces[namespaceId] ?? {
          uuid: namespaceId,
          name: namespaceName,
          tableEligibilityCount: {
            ineligible: 0,
            eligibleInCurrentConfig: 0
          },
          sizeBytes: 0,
          tables: [],
          allTables: []
        };
        if (XCLUSTER_TABLE_INELIGIBLE_STATUSES.includes(eligibilityDetails.status)) {
          items.namespaces[namespaceId].tableEligibilityCount.ineligible += 1;
        } else if (
          eligibilityDetails.status === XClusterTableEligibility.ELIGIBLE_IN_CURRENT_CONFIG
        ) {
          items.namespaces[namespaceId].tableEligibilityCount.eligibleInCurrentConfig += 1;
        }

        // Selecting/deselecting a namespace will select/deselect all tables under that namespace regardless if
        // those tables match the current filter.
        items.namespaces[namespaceId].allTables.push(mainTableReplicationCandidate);
        items.namespaces[namespaceId].sizeBytes += sizeBytes + indexTablesTotalSize;

        // Metadata, `tables`, `searchMatchTableUuids`, `searchMatchingNamespaceUuids` fields reflect what is presented
        // to the user based on the current search tokens.
        const isMatchedBySearchTokens =
          isTableMatchedBySearchTokens(
            mainTableReplicationCandidate,
            searchTokens,
            unreplicatedTableInReplicatedNamespace
          ) ||
          mainTableReplicationCandidate.indexTables?.some((indexTableReplicationCandidate) =>
            isTableMatchedBySearchTokens(
              indexTableReplicationCandidate,
              searchTokens,
              unreplicatedTableInReplicatedNamespace
            )
          );
        if (isMatchedBySearchTokens) {
          // Push tables and update metadata
          mainTableReplicationCandidate.indexTableIDs?.forEach((tableUuid) =>
            items.searchMatchingTableUuids.add(tableUuid)
          );
          items.searchMatchingTableUuids.add(mainTableReplicationCandidate.tableUUID);
          items.searchMatchingNamespaceUuids.add(namespaceId);
          items.namespaces[namespaceId].tables.push(mainTableReplicationCandidate);
        }
      }
      return items;
    },
    {
      namespaces: {},
      searchMatchingTableUuids: new Set<string>(),
      searchMatchingNamespaceUuids: new Set<string>()
    }
  );
  return {
    replicationItems,
    namespaceUuidToUnreplicatedTableCandidates
  };
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
    const tableUuidToTableDetails = getInConfigTableUuidsToTableDetailsMap(
      xClusterConfig.tableDetails
    );
    if (tableUuidToTableDetails.has(sourceTable.tableID)) {
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
 * - Allow selecting namespaces which do not contain ineligible tables.
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
        // if they got into that state somehow.
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

/**
 * Fields to do substring search on if search token modifier is not specified.
 */
const SUBSTRING_SEARCH_FIELDS = ['table', 'database'];

const isTableMatchedBySearchTokens = (
  table: TableReplicationCandidate,
  searchTokens: SearchToken[],
  unreplicatedTableInReplicatedNamespace?: Set<string>
) => {
  const candidate = {
    database: { value: table.keySpace, type: FieldType.STRING },
    table: { value: table.tableName, type: FieldType.STRING },
    sizeBytes: { value: table.sizeBytes, type: FieldType.NUMBER },
    ...(unreplicatedTableInReplicatedNamespace && {
      unreplicatedTable: {
        value: unreplicatedTableInReplicatedNamespace.has(getTableUuid(table)),
        type: FieldType.BOOLEAN
      }
    })
  };
  return searchTokens.every((searchToken) =>
    isMatchedBySearchToken(candidate, searchToken, SUBSTRING_SEARCH_FIELDS)
  );
};
