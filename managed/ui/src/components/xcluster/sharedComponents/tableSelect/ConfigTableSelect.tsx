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
import { Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { fetchTablesInUniverse } from '../../../../actions/xClusterReplication';
import { api, metricQueryKey, universeQueryKey } from '../../../../redesign/helpers/api';
import { YBControlledSelect, YBInputField } from '../../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { hasSubstringMatch } from '../../../queries/helpers/queriesHelper';
import { formatBytes, augmentTablesWithXClusterDetails, tableSort } from '../../ReplicationUtils';
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

import {
  MetricsQueryParams,
  TableType,
  Universe,
  YBTable
} from '../../../../redesign/helpers/dtos';
import { XClusterTable, XClusterTableType } from '../../XClusterTypes';
import { XClusterConfig } from '../../dtos';
import { NodeAggregation, SplitType } from '../../../metrics/dtos';
import { ExpandColumnComponent } from './ExpandColumnComponent';

import styles from './ConfigTableSelect.module.scss';

interface RowItem {
  namespace: string;
  sizeBytes: number;
  xClusterTables: XClusterTable[];
}

interface ConfigTableSelectProps {
  xClusterConfig: XClusterConfig;
  isDrInterface: boolean;
  setSelectedTableUUIDs: (tableUUIDs: string[]) => void;
  selectedTableUUIDs: string[];
  configTableType: XClusterTableType;
  selectedNamespaces: string[];
  setSelectedNamespaces: (selectedNamespaces: string[]) => void;
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
  selectedTableUUIDs,
  setSelectedTableUUIDs,
  isDrInterface,
  configTableType,
  selectedNamespaces,
  setSelectedNamespaces,
  selectionError,
  selectionWarning
}: ConfigTableSelectProps) => {
  const [namespaceSearchTerm, setNamespaceSearchTerm] = useState('');
  const [pageSize, setPageSize] = useState(PAGE_SIZE_OPTIONS[0]);
  const [activePage, setActivePage] = useState(1);
  const [sortField, setSortField] = useState<keyof RowItem>('namespace');
  const [sortOrder, setSortOrder] = useState<ReactBSTableSortOrder>(SortOrder.ASCENDING);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

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
  const replciationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [replicationLagMetricSettings],
    nodePrefix: sourceUniverseQuery.data?.universeDetails.nodePrefix,
    xClusterConfigUuid: xClusterConfig.uuid,
    start: moment().subtract(liveMetricTimeRangeValue, liveMetricTimeRangeUnit).format('X'),
    end: moment().format('X')
  };
  const tableReplicationLagQuery = useQuery(
    metricQueryKey.live(
      replciationLagMetricRequestParams,
      liveMetricTimeRangeValue,
      liveMetricTimeRangeUnit
    ),
    () => api.fetchMetrics(replciationLagMetricRequestParams),
    {
      enabled: !!sourceUniverseQuery.data
    }
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

  const toggleTableGroup = (isSelected: boolean, xClusterTables: XClusterTable[]) => {
    if (isSelected) {
      const currentSelectedTableUuids = new Set(selectedTableUUIDs);

      xClusterTables.forEach((xClusterTable) => {
        currentSelectedTableUuids.add(getTableUuid(xClusterTable));
      });

      setSelectedTableUUIDs(Array.from(currentSelectedTableUuids));
    } else {
      const removedTableUuids = new Set(
        xClusterTables.map((xClustertables) => getTableUuid(xClustertables))
      );

      setSelectedTableUUIDs(
        selectedTableUUIDs.filter((tableUUID) => !removedTableUuids.has(tableUUID))
      );
    }
  };

  const handleTableGroupToggle = (isSelected: boolean, xClusterTables: XClusterTable[]) => {
    toggleTableGroup(isSelected, xClusterTables);
    return true;
  };

  const handleTableToggle = (xClustertable: XClusterTable, isSelected: boolean) => {
    toggleTableGroup(isSelected, [xClustertable]);
  };

  const toggleNamespaceGroup = (isSelected: boolean, rows: RowItem[]) => {
    if (isSelected) {
      const currentSelectedNamespaces = new Set(selectedNamespaces);

      rows.forEach((row) => {
        currentSelectedNamespaces.add(row.namespace);
      });
      setSelectedNamespaces(Array.from(currentSelectedNamespaces));
    } else {
      const removedNamespaceUuids = new Set(rows.map((row) => row.namespace));

      setSelectedNamespaces(
        selectedNamespaces.filter((namespace: string) => !removedNamespaceUuids.has(namespace))
      );
    }
  };

  const handleAllNamespaceSelect = (isSelected: boolean, rows: RowItem[]) => {
    const selectedTables = rows.reduce((table: XClusterTable[], row) => {
      return table.concat(row.xClusterTables);
    }, []);

    toggleNamespaceGroup(isSelected, rows);
    toggleTableGroup(isSelected, selectedTables);
    return true;
  };

  const handleNamespaceSelect = (row: RowItem, isSelected: boolean) => {
    toggleNamespaceGroup(isSelected, [row]);
    toggleTableGroup(isSelected, row.xClusterTables);
  };

  const tablesInConfig = augmentTablesWithXClusterDetails(
    sourceUniverseTablesQuery.data,
    xClusterConfig.tableDetails,
    tableReplicationLagQuery.data?.async_replication_sent_lag?.data
  );

  const tablesForSelection = tablesInConfig.filter(
    (xClusterTable) => xClusterTable.tableType !== TableType.TRANSACTION_STATUS_TABLE_TYPE
  );
  const rowItems = getRowItemsFromTables(tablesForSelection);
  const tableOptions: Options = {
    sortName: sortField,
    sortOrder: sortOrder,
    onSortChange: (sortName: string | number | symbol, sortOrder: ReactBSTableSortOrder) => {
      // Each row of the table is of type RowItem.
      setSortField(sortName as keyof RowItem);
      setSortOrder(sortOrder);
    }
  };
  const tableDescriptor = isDrInterface
    ? t('selectionDescriptorDr')
    : t('selectionDescriptorXCluster');
  const sourceUniverseUUID = xClusterConfig.sourceUniverseUUID;
  return (
    <>
      <div className={styles.tableDescriptor}>{tableDescriptor}</div>
      <div className={styles.tableToolbar}>
        <YBInputField
          containerClassName={styles.namespaceSearchInput}
          placeHolder="Search for database.."
          onValueChanged={(searchTerm: string) => setNamespaceSearchTerm(searchTerm)}
        />
      </div>
      <div className={styles.bootstrapTableContainer}>
        <BootstrapTable
          tableContainerClass={styles.bootstrapTable}
          maxHeight="450px"
          data={rowItems
            .filter((row) => hasSubstringMatch(row.namespace, namespaceSearchTerm))
            .sort((a, b) => tableSort<RowItem>(a, b, sortField, sortOrder, 'namespace'))
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
              handleTableSelect={handleTableToggle}
              handleAllTableSelect={handleTableGroupToggle}
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
            onSelect: handleNamespaceSelect,
            onSelectAll: handleAllNamespaceSelect,
            selected: selectedNamespaces
          }}
          options={tableOptions}
        >
          <TableHeaderColumn dataField="namespace" isKey={true} dataSort={true}>
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
      {configTableType === TableType.YQL_TABLE_TYPE ? (
        <Typography variant="body2">
          {t('tableSelectionCount', {
            selectedTableCount: selectedTableUUIDs.length,
            availableTableCount: tablesForSelection.length
          })}
        </Typography>
      ) : (
        <Typography variant="body2">
          {t('databaseSelectionCount', {
            selectedDatabaseCount: selectedNamespaces.length,
            availableDatabaseCount: rowItems.length
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

function getRowItemsFromTables(xClusterConfigTables: XClusterTable[]): RowItem[] {
  /**
   * Map from namespace name to namespace details.
   */
  const namespaceToNamespaceDetails = new Map<
    string,
    { xClusterTables: XClusterTable[]; sizeBytes: number }
  >();
  xClusterConfigTables.forEach((xClusterTable) => {
    const namespaceDetails = namespaceToNamespaceDetails.get(xClusterTable.keySpace);
    if (namespaceDetails !== undefined) {
      namespaceDetails.xClusterTables.push(xClusterTable);
      namespaceDetails.sizeBytes += xClusterTable.sizeBytes;
    } else {
      namespaceToNamespaceDetails.set(xClusterTable.keySpace, {
        xClusterTables: [xClusterTable],
        sizeBytes: xClusterTable.sizeBytes
      });
    }
  });
  return Array.from(namespaceToNamespaceDetails, ([namespace, namespaceDetails]) => ({
    namespace: namespace,
    ...namespaceDetails
  }));
}
