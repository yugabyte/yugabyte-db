import { useState } from 'react';
import {
  BootstrapTable,
  Options,
  SortOrder as ReactBSTableSortOrder,
  TableHeaderColumn
} from 'react-bootstrap-table';
import { useQuery } from 'react-query';
import clsx from 'clsx';
import moment from 'moment';
import { Box, Typography, useTheme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { fetchTablesInUniverse } from '../../../../actions/xClusterReplication';
import {
  alertConfigQueryKey,
  api,
  metricQueryKey,
  universeQueryKey
} from '../../../../redesign/helpers/api';
import { YBControlledSelect } from '../../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import {
  formatBytes,
  augmentTablesWithXClusterDetails,
  tableSort,
  getStrictestReplicationLagAlertThreshold,
  getNamespaceIdentifierToNamespaceUuidMap,
  getNamespaceIdentifier
} from '../../ReplicationUtils';
import YBPagination from '../../../tables/YBPagination/YBPagination';
import { ExpandedConfigTableSelect } from './ExpandedConfigTableSelect';
import { SortOrder } from '../../../../redesign/helpers/constants';
import {
  liveMetricTimeRangeUnit,
  liveMetricTimeRangeValue,
  MetricName,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../../constants';
import { getTableUuid } from '../../../../utils/tableUtils';
import { getAlertConfigurations } from '../../../../actions/universe';
import { AlertTemplate } from '../../../../redesign/features/alerts/TemplateComposer/ICustomVariables';
import { ExpandColumnComponent } from './ExpandColumnComponent';
import {
  FieldType,
  isMatchedBySearchToken
} from '../../../../redesign/components/YBSmartSearchBar/helpers';
import {
  SearchToken,
  YBSmartSearchBar
} from '../../../../redesign/components/YBSmartSearchBar/YBSmartSearchBar';

import {
  MetricsQueryParams,
  TableType,
  Universe,
  UniverseNamespace,
  YBTable
} from '../../../../redesign/helpers/dtos';
import {
  IndexTableRestartReplicationCandidate,
  MainTableRestartReplicationCandidate,
  TableRestartReplicationCandidate,
  XClusterTable,
  XClusterTableType
} from '../../XClusterTypes';
import { XClusterConfig } from '../../dtos';
import { NodeAggregation, SplitType } from '../../../metrics/dtos';

import styles from './ConfigTableSelect.module.scss';

interface NamespaceItem {
  uuid: string;
  name: string;
  // Total size for all table in the namespace (regardless of search token match)
  sizeBytes: number;

  // Stores the xCluster tables which match the current search tokens
  xClusterTables: MainTableRestartReplicationCandidate[];
  // Stores all xCluster tables in the namespace
  allXClusterTables: MainTableRestartReplicationCandidate[];
}

interface SelectionOptions {
  namespaces: NamespaceItem[];

  // We store a set of table uuids at the top level to make it easy to check
  // if the list of table options matching the current search tokens contains a specific table uuid.
  searchMatchingTableUuids: Set<string>;
  searchMatchingNamespaceUuids: Set<string>;
}

interface ConfigTableSelectProps {
  xClusterConfig: XClusterConfig;
  isDrInterface: boolean;
  setSelectedTableUuids: (tableUUIDs: string[]) => void;
  selectedTableUuids: string[];
  configTableType: XClusterTableType;
  selectedNamespaceUuids: string[];
  setSelectedNamespaceUuids: (selectedNamespaceUuids: string[]) => void;
  selectionError: { title?: string; body?: string } | undefined;
  selectionWarning: { title: string; body: string } | undefined;
}

const TABLE_MIN_PAGE_SIZE = 10;
const PAGE_SIZE_OPTIONS = [TABLE_MIN_PAGE_SIZE, 20, 30, 40] as const;
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.selectTable';

/**
 * Input component for selecting tables for xCluster configuration.
 * The state of selected tables and namespaces is controlled externally.
 */
export const ConfigTableSelect = ({
  xClusterConfig,
  selectedTableUuids,
  setSelectedTableUuids,
  isDrInterface,
  configTableType,
  selectedNamespaceUuids,
  setSelectedNamespaceUuids,
  selectionError,
  selectionWarning
}: ConfigTableSelectProps) => {
  const [searchTokens, setSearchTokens] = useState<SearchToken[]>([]);
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof NamespaceItem>('name');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();

  const sourceUniverseNamespaceQuery = useQuery<UniverseNamespace[]>(
    universeQueryKey.namespaces(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverseNamespaces(xClusterConfig.sourceUniverseUUID)
  );

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(xClusterConfig.sourceUniverseUUID, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(
        xClusterConfig.sourceUniverseUUID,
        XCLUSTER_UNIVERSE_TABLE_FILTERS
      ).then((response) => response.data)
  );
  const sourceUniverseQuery = useQuery<Universe>(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  const replicationLagMetricSettings = {
    metric: MetricName.ASYNC_REPLICATION_SENT_LAG,
    nodeAggregation: NodeAggregation.MAX,
    splitType: SplitType.TABLE
  };
  const replicationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [replicationLagMetricSettings],
    nodePrefix: sourceUniverseQuery.data?.universeDetails.nodePrefix,
    xClusterConfigUuid: xClusterConfig.uuid,
    start: moment().subtract(liveMetricTimeRangeValue, liveMetricTimeRangeUnit).format('X'),
    end: moment().format('X')
  };
  const tableReplicationLagQuery = useQuery(
    metricQueryKey.live(
      replicationLagMetricRequestParams,
      liveMetricTimeRangeValue,
      liveMetricTimeRangeUnit
    ),
    () => api.fetchMetrics(replicationLagMetricRequestParams),
    {
      enabled: !!sourceUniverseQuery.data
    }
  );
  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: xClusterConfig.sourceUniverseUUID
  };
  const replicationLagAlertConfigQuery = useQuery(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );

  if (
    xClusterConfig.sourceUniverseUUID === undefined ||
    xClusterConfig.targetUniverseUUID === undefined
  ) {
    const errorMessage =
      xClusterConfig.sourceUniverseUUID === undefined
        ? 'The xCluster config has undefined sourceUniverseUUID. This indicates that the source universe might be deleted.'
        : 'The xCluster config has undefined targetUniverseUUID. This indicates that the target universe might be deleted.';
    return <YBErrorIndicator customErrorMessage={errorMessage} />;
  }

  if (
    sourceUniverseNamespaceQuery.isLoading ||
    sourceUniverseNamespaceQuery.isIdle ||
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle
  ) {
    return <YBLoading />;
  }

  const sourceUniverseLabel = isDrInterface ? 'DR primary universe' : 'source universe';
  if (
    sourceUniverseNamespaceQuery.isError ||
    sourceUniverseTablesQuery.isError ||
    sourceUniverseQuery.isError
  ) {
    return (
      <YBErrorIndicator customErrorMessage={`Error fetching ${sourceUniverseLabel} information.`} />
    );
  }

  const toggleTableGroup = (
    isSelected: boolean,
    xClusterTables: TableRestartReplicationCandidate[]
  ) => {
    if (isSelected) {
      const currentSelectedTableUuids = new Set(selectedTableUuids);

      xClusterTables.forEach((xClusterTable) => {
        currentSelectedTableUuids.add(getTableUuid(xClusterTable));
      });

      setSelectedTableUuids(Array.from(currentSelectedTableUuids));
    } else {
      const removedTableUuids = new Set(
        xClusterTables.map((xClusterTables) => getTableUuid(xClusterTables))
      );

      setSelectedTableUuids(
        selectedTableUuids.filter((tableUUID) => !removedTableUuids.has(tableUUID))
      );
    }
  };

  const handleTableGroupToggle = (
    isSelected: boolean,
    xClusterTables: TableRestartReplicationCandidate[]
  ) => {
    toggleTableGroup(isSelected, xClusterTables);
    return true;
  };

  const handleTableToggle = (
    xClusterTable: TableRestartReplicationCandidate,
    isSelected: boolean
  ) => {
    toggleTableGroup(isSelected, [xClusterTable]);
  };

  const toggleNamespaceGroup = (isSelected: boolean, namespaceItems: NamespaceItem[]) => {
    if (isSelected) {
      const currentSelectedNamespaceUuids = new Set(selectedNamespaceUuids);

      namespaceItems.forEach((namespaceItem) => {
        currentSelectedNamespaceUuids.add(namespaceItem.uuid);
      });
      setSelectedNamespaceUuids(Array.from(currentSelectedNamespaceUuids));
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
    const selectedTables = namespaceItems.reduce(
      (table: TableRestartReplicationCandidate[], namespaceItem) => {
        return table.concat(namespaceItem.allXClusterTables);
      },
      []
    );

    toggleNamespaceGroup(isSelected, namespaceItems);
    toggleTableGroup(isSelected, selectedTables);
    return true;
  };

  const handleNamespaceToggle = (row: NamespaceItem, isSelected: boolean) => {
    toggleNamespaceGroup(isSelected, [row]);
    toggleTableGroup(isSelected, row.allXClusterTables);
  };

  const handleSearchTokenChange = (searchTokens: SearchToken[]) => {
    setSearchTokens(searchTokens);
    setActivePage(1);
  };

  const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(
    replicationLagAlertConfigQuery.data
  );
  const tablesInConfig = augmentTablesWithXClusterDetails(
    sourceUniverseTablesQuery.data,
    xClusterConfig.tableDetails,
    maxAcceptableLag,
    tableReplicationLagQuery.data?.async_replication_sent_lag?.data,
    { includeDroppedTables: false }
  );

  const tablesForSelection = tablesInConfig.filter(
    (xClusterTable) => xClusterTable.tableType !== TableType.TRANSACTION_STATUS_TABLE_TYPE
  );
  const selectionOptions = getSelectionOptionsFromTables(
    sourceUniverseNamespaceQuery.data,
    tablesForSelection,
    searchTokens
  );
  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type RowItem.
      setSortField(sortName as keyof NamespaceItem);
      setSortOrder(sortOrder);
    }
  };
  const selectedSearchMatchingTableCount: number = selectedTableUuids.filter((tableUuid) =>
    selectionOptions.searchMatchingTableUuids.has(tableUuid)
  ).length;
  const selectedSearchMatchingNamespaceCount: number = selectedNamespaceUuids.filter(
    (namespaceUuid) => selectionOptions.searchMatchingNamespaceUuids.has(namespaceUuid)
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
          recognizedModifiers={['database', 'table']}
          placeholder={t('tablesSearchBarPlaceholder')}
        />
      </Box>
      <div className={styles.bootstrapTableContainer}>
        <BootstrapTable
          tableContainerClass={styles.bootstrapTable}
          maxHeight="450px"
          data={selectionOptions.namespaces
            .sort((a, b) => tableSort<NamespaceItem>(a, b, sortField, sortOrder, 'name'))
            .slice((activePage - 1) * pageSize, activePage * pageSize)}
          expandableRow={(row: NamespaceItem) => {
            return row.xClusterTables.length > 0;
          }}
          expandComponent={(row: NamespaceItem) => (
            <ExpandedConfigTableSelect
              tables={row.xClusterTables}
              selectedTableUuids={selectedTableUuids}
              tableType={configTableType}
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
            selected: selectedNamespaceUuids
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
      {selectionOptions.namespaces.length > TABLE_MIN_PAGE_SIZE && (
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
            numPages={Math.ceil(selectionOptions.namespaces.length / pageSize)}
            onChange={(newPageNum: number) => {
              setActivePage(newPageNum);
            }}
            activePage={activePage}
          />
        </div>
      )}
      {configTableType === TableType.YQL_TABLE_TYPE ? (
        <Typography variant="body2">
          {t('tableSelectionCount', {
            selectedTableCount: selectedSearchMatchingTableCount,
            availableTableCount: selectionOptions.searchMatchingTableUuids.size
          })}
        </Typography>
      ) : (
        <Typography variant="body2">
          {t('databaseSelectionCount', {
            selectedDatabaseCount: selectedSearchMatchingNamespaceCount,
            availableDatabaseCount: selectionOptions.searchMatchingNamespaceUuids.size
          })}
        </Typography>
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

function getSelectionOptionsFromTables(
  sourceUniverseNamespaces: UniverseNamespace[],
  xClusterConfigTables: XClusterTable[],
  searchTokens: SearchToken[]
): SelectionOptions {
  const searchMatchingNamespaceUuids = new Set<string>();
  const searchMatchingTableUuids = new Set<string>();
  const namespaceUuidToNamespaceDetails = new Map<string, NamespaceItem>();
  const namespaceIdentifierToNamespaceUuid = getNamespaceIdentifierToNamespaceUuidMap(
    sourceUniverseNamespaces
  );
  const tableUuidToConfigTable = Object.fromEntries(
    xClusterConfigTables.map((table) => [getTableUuid(table), table])
  );

  xClusterConfigTables.forEach((xClusterTable) => {
    // Filter out index tables because we will add them as a field under the main table.
    if (!xClusterTable.isIndexTable) {
      // Add associated index tables if the current source table is a main table.
      const indexTables = [] as IndexTableRestartReplicationCandidate[];
      let indexTableTotalSize = 0;
      xClusterTable.indexTableIDs?.forEach((indexTableUuid) => {
        const indexTable = tableUuidToConfigTable[indexTableUuid];
        if (indexTable) {
          indexTables.push(indexTable);
          indexTableTotalSize += indexTable.sizeBytes;
        }
      });

      const mainTableRestartReplicationCandidate: MainTableRestartReplicationCandidate = {
        ...xClusterTable,
        indexTables
      };

      const namespaceName = xClusterTable.keySpace;
      const namespaceUuid =
        namespaceIdentifierToNamespaceUuid[
          getNamespaceIdentifier(namespaceName, xClusterTable.tableType)
        ];
      const isTableMatchedBySearchTokens =
        checkIsTableMatchedBySearchTokens(xClusterTable, searchTokens) ||
        mainTableRestartReplicationCandidate.indexTables?.some(
          (indexTableRestartReplicationCandidate) =>
            checkIsTableMatchedBySearchTokens(indexTableRestartReplicationCandidate, searchTokens)
        );
      if (isTableMatchedBySearchTokens) {
        searchMatchingNamespaceUuids.add(namespaceUuid);
        searchMatchingTableUuids.add(getTableUuid(xClusterTable));
      }

      const namespaceDetails = namespaceUuidToNamespaceDetails.get(namespaceUuid);
      if (namespaceDetails !== undefined) {
        // Selecting/deselecting a namespace will select/deselect all tables under that namespace regardless if
        // those tables match the current filter.
        namespaceDetails.allXClusterTables.push(mainTableRestartReplicationCandidate);
        namespaceDetails.sizeBytes += xClusterTable.sizeBytes + indexTableTotalSize;
        if (isTableMatchedBySearchTokens) {
          namespaceDetails.xClusterTables.push(mainTableRestartReplicationCandidate);
        }
      } else {
        namespaceUuidToNamespaceDetails.set(namespaceUuid, {
          uuid: namespaceUuid,
          name: namespaceName,
          sizeBytes: xClusterTable.sizeBytes + indexTableTotalSize,
          xClusterTables: isTableMatchedBySearchTokens
            ? [mainTableRestartReplicationCandidate]
            : [],
          allXClusterTables: [mainTableRestartReplicationCandidate]
        });
      }
    }
  });
  const namespaces = Array.from(namespaceUuidToNamespaceDetails, ([_, namespaceDetails]) => ({
    ...namespaceDetails
  }));

  return {
    namespaces,
    searchMatchingTableUuids,
    searchMatchingNamespaceUuids
  };
}

/**
 * Fields to do substring search on if search token modifier is not specified.
 */
const SUBSTRING_SEARCH_FIELDS = ['table', 'database'];

const checkIsTableMatchedBySearchTokens = (table: XClusterTable, searchTokens: SearchToken[]) => {
  const candidate = {
    database: { value: table.keySpace, type: FieldType.STRING },
    table: { value: table.tableName, type: FieldType.STRING }
  };
  return searchTokens.every((searchToken) =>
    isMatchedBySearchToken(candidate, searchToken, SUBSTRING_SEARCH_FIELDS)
  );
};
