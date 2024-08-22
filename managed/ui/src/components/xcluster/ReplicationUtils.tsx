import { useQuery } from 'react-query';
import moment from 'moment';
import i18n from 'i18next';

import { getAlertConfigurations } from '../../actions/universe';
import { fetchReplicationLag } from '../../actions/xClusterReplication';
import { formatLagMetric } from '../../utils/Formatters';
import {
  MetricName,
  MetricTraceName,
  XClusterConfigAction,
  XClusterConfigStatus,
  BROKEN_XCLUSTER_CONFIG_STATUSES,
  XClusterConfigType,
  XClusterTableEligibility,
  XClusterTableStatus,
  TRANSACTIONAL_ATOMICITY_YB_SOFTWARE_VERSION_THRESHOLD,
  XCLUSTER_UNDEFINED_LAG_NUMERIC_REPRESENTATION,
  I18N_KEY_PREFIX_XCLUSTER_TABLE_STATUS,
  UNCONFIGURED_XCLUSTER_TABLE_STATUSES,
  DROPPED_XCLUSTER_TABLE_STATUSES,
  BootstrapCategory
} from './constants';
import {
  alertConfigQueryKey,
  api,
  metricQueryKey,
  universeQueryKey
} from '../../redesign/helpers/api';
import { getUniverseStatus } from '../universes/helpers/universeHelpers';
import { UnavailableUniverseStates } from '../../redesign/helpers/constants';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';
import { SortOrder } from '../../redesign/helpers/constants';
import { compareYBSoftwareVersions, getPrimaryCluster } from '../../utils/universeUtilsTyped';

import {
  Metrics,
  MetricTimeRange,
  StandardMetricTimeRangeOption,
  XClusterReplicationTable,
  MainTableReplicationCandidate,
  IndexTableReplicationCandidate,
  XClusterTableType,
  XClusterTable,
  CategorizedNeedBootstrapPerTableResponse
} from './XClusterTypes';
import {
  XClusterConfig,
  XClusterConfigNeedBootstrapPerTableResponse,
  XClusterNeedBootstrapReason,
  XClusterTableDetails
} from './dtos';
import {
  MetricTrace,
  TableType,
  Universe,
  UniverseNamespace,
  YBTable
} from '../../redesign/helpers/dtos';
import {
  AlertTemplate,
  AlertThresholdCondition,
  IAlertConfiguration as AlertConfiguration
} from '../../redesign/features/alerts/TemplateComposer/ICustomVariables';
import { DrConfigState } from './disasterRecovery/dtos';

import './ReplicationUtils.scss';

// TODO: Rename, refactor and pull into separate file
export const MaxAcceptableLag = ({
  currentUniverseUUID
}: {
  currentUniverseUUID: string | undefined;
}) => {
  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: currentUniverseUUID
  };
  const replicationLagAlertConfigQuery = useQuery(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );

  if (replicationLagAlertConfigQuery.isLoading || replicationLagAlertConfigQuery.isIdle) {
    return <i className="fa fa-spinner fa-spin yb-spinner"></i>;
  }
  if (replicationLagAlertConfigQuery.isError || replicationLagAlertConfigQuery.data.length === 0) {
    return <span>-</span>;
  }

  const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(
    replicationLagAlertConfigQuery.data
  );
  return <span>{maxAcceptableLag ? formatLagMetric(maxAcceptableLag) : '-'}</span>;
};

// TODO: Rename, refactor and pull into separate file
export const CurrentReplicationLag = ({
  xClusterConfigUuid,
  xClusterConfigStatus,
  sourceUniverseUuid
}: {
  xClusterConfigUuid: string;
  xClusterConfigStatus: XClusterConfigStatus;
  sourceUniverseUuid: string | undefined;
}) => {
  const sourceUniverseQuery = useQuery(universeQueryKey.detail(sourceUniverseUuid), () =>
    api.fetchUniverse(sourceUniverseUuid)
  );

  const replicationLagMetricRequestParams = {
    nodePrefix: sourceUniverseQuery.data?.universeDetails.nodePrefix,
    xClusterConfigUuid
  };
  const universeLagQuery = useQuery(
    metricQueryKey.live(replicationLagMetricRequestParams, '1', 'hour'),
    () => fetchReplicationLag(replicationLagMetricRequestParams),
    {
      enabled: !!sourceUniverseQuery.data
    }
  );

  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: sourceUniverseUuid
  };
  const replicationLagAlertConfigQuery = useQuery(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );

  if (
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    universeLagQuery.isLoading ||
    universeLagQuery.isIdle ||
    replicationLagAlertConfigQuery.isLoading ||
    replicationLagAlertConfigQuery.isIdle
  ) {
    return <i className="fa fa-spinner fa-spin yb-spinner" />;
  }

  if (
    BROKEN_XCLUSTER_CONFIG_STATUSES.includes(xClusterConfigStatus) ||
    sourceUniverseQuery.isError ||
    universeLagQuery.isError ||
    replicationLagAlertConfigQuery.isError
  ) {
    return <span>-</span>;
  }

  const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(
    replicationLagAlertConfigQuery.data
  );
  const maxNodeLag = getLatestMaxNodeLag(universeLagQuery.data);
  const formattedLag = formatLagMetric(maxNodeLag);
  const isReplicationUnhealthy =
    maxNodeLag === undefined || (maxAcceptableLag && maxNodeLag > maxAcceptableLag);

  if (maxNodeLag === undefined) {
    return <span className="replication-lag-value warning">{formattedLag}</span>;
  }

  return (
    <span
      className={`replication-lag-value ${
        isReplicationUnhealthy ? 'above-threshold' : 'below-threshold'
      }`}
    >
      {isReplicationUnhealthy && <i className="fa fa-exclamation-triangle" aria-hidden="true" />}
      {formattedLag}
    </span>
  );
};

// TODO: Rename, refactor and pull into separate file
export const CurrentTableReplicationLag = ({
  tableId,
  streamId,
  queryEnabled,
  nodePrefix,
  sourceUniverseUUID,
  xClusterConfigStatus
}: {
  tableId: string;
  streamId: string;
  queryEnabled: boolean;
  nodePrefix: string | undefined;
  sourceUniverseUUID: string | undefined;
  xClusterConfigStatus: XClusterConfigStatus;
}) => {
  const replicationLagMetricRequestParams = {
    nodePrefix,
    streamId,
    tableId
  };
  const tableLagQuery = useQuery(
    metricQueryKey.live(replicationLagMetricRequestParams, '1', 'hour'),
    () => fetchReplicationLag(replicationLagMetricRequestParams),
    {
      enabled: queryEnabled
    }
  );

  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: sourceUniverseUUID
  };
  const replicationLagAlertConfigQuery = useQuery(
    alertConfigQueryKey.list(alertConfigFilter),
    () => getAlertConfigurations(alertConfigFilter),
    {
      enabled: queryEnabled
    }
  );

  if (
    tableLagQuery.isLoading ||
    tableLagQuery.isIdle ||
    replicationLagAlertConfigQuery.isLoading ||
    replicationLagAlertConfigQuery.isIdle
  ) {
    return <i className="fa fa-spinner fa-spin yb-spinner" />;
  }

  if (
    BROKEN_XCLUSTER_CONFIG_STATUSES.includes(xClusterConfigStatus) ||
    tableLagQuery.isError ||
    replicationLagAlertConfigQuery.isError
  ) {
    return <span>-</span>;
  }

  const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(
    replicationLagAlertConfigQuery.data
  );
  const maxNodeLag = getLatestMaxNodeLag(tableLagQuery.data);
  const formattedLag = formatLagMetric(maxNodeLag);
  const isReplicationUnhealthy =
    maxNodeLag === undefined || (maxAcceptableLag && maxNodeLag > maxAcceptableLag);

  if (maxNodeLag === undefined) {
    return <span className="replication-lag-value warning">{formattedLag}</span>;
  }

  return (
    <span
      className={`replication-lag-value ${
        isReplicationUnhealthy ? 'above-threshold' : 'below-threshold'
      }`}
    >
      {isReplicationUnhealthy && <i className="fa fa-exclamation-triangle" aria-hidden="true" />}
      {formattedLag}
    </span>
  );
};

export const getLatestMaxNodeLag = (metric: Metrics<'tserver_async_replication_lag_micros'>) => {
  const lagMetric = metric.tserver_async_replication_lag_micros;
  const traceAlias =
    lagMetric.layout.yaxis.alias[
      MetricTraceName[MetricName.TSERVER_ASYNC_REPLICATION_LAG].COMMITTED_LAG
    ];
  const traces = lagMetric.data.filter((trace) => trace.name === traceAlias);
  const latestLags = traces.reduce((latestLags: number[], trace) => {
    const latestLag = parseFloatIfDefined(trace.y[trace.y.length - 1]);
    if (latestLag !== undefined) {
      latestLags.push(latestLag);
    }
    return latestLags;
  }, []);
  return latestLags.length ? Math.max(...latestLags) : undefined;
};

export const getMaxNodeLagMetric = (
  metric: Metrics<'tserver_async_replication_lag_micros'>
): MetricTrace | undefined => {
  const lagMetric = metric.tserver_async_replication_lag_micros;
  const traceAlias =
    lagMetric.layout.yaxis.alias[
      MetricTraceName[MetricName.TSERVER_ASYNC_REPLICATION_LAG].COMMITTED_LAG
    ];
  const traces = lagMetric.data.filter((trace) => trace.name === traceAlias);
  if (!traces.length) {
    return undefined;
  }

  // Take the maximum y at every x across all nodes.
  const maxY = new Array<number>(traces[0].y.length).fill(0);
  traces.forEach((trace) => {
    trace.y.forEach((y: string | number, idx: number) => {
      maxY[idx] = Math.max(maxY[idx], parseFloatIfDefined(y) ?? 0);
    });
  });

  return {
    ...traces[0],
    name: `Max ${traceAlias}`,
    y: maxY
  };
};

// Improvement: Consider if we can do this data transform on the server.
export const adaptMetricDataForRecharts = (
  metricTraces: MetricTrace[],
  getUniqueTraceName: (trace: MetricTrace) => string
): { [dataKey: string]: number | null }[] => {
  const combinedDataPoints = new Map<number, { x: number; [dataKey: string]: number | null }>();
  metricTraces.forEach((trace) => {
    const traceName = getUniqueTraceName(trace);
    trace.x.forEach((x, index) => {
      // `null` values will be reflected as a gap in the chart unless we ask
      // Recharts to connect gaps.
      const y = parseFloatIfDefined(trace.y[index]) ?? null;
      const combinedY = combinedDataPoints.get(x);
      if (combinedY !== undefined) {
        combinedY[traceName] = y;
      } else {
        combinedDataPoints.set(x, { x, [traceName]: y });
      }
    });
  });
  return Array.from(combinedDataPoints.values()).sort(
    (dataPointA, dataPointB) => dataPointA.x - dataPointB.x
  );
};

export const getCategorizedNeedBootstrapPerTableResponse = (
  xClusterConfigNeedBootstrapPerTableResponse: XClusterConfigNeedBootstrapPerTableResponse
): CategorizedNeedBootstrapPerTableResponse => {
  const categorizedNeedBootstrapPerTableResponse: CategorizedNeedBootstrapPerTableResponse = {
    bootstrapTableUuids: [],
    noBootstrapRequired: {
      bootstrapCategory: BootstrapCategory.NO_BOOTSTRAP_REQUIRED,
      tableCount: 0,
      tables: {}
    },
    tableHasDataBidirectional: {
      bootstrapCategory: BootstrapCategory.TABLE_HAS_DATA_BIDIRECTIONAL,
      tableCount: 0,
      tables: {}
    },
    targetTableMissingBidirectional: {
      bootstrapCategory: BootstrapCategory.TARGET_TABLE_MISSING_BIDIRECTIONAL,
      tableCount: 0,
      tables: {}
    },
    tableHasData: {
      bootstrapCategory: BootstrapCategory.TABLE_HAS_DATA,
      tableCount: 0,
      tables: {}
    },
    targetTableMissing: {
      bootstrapCategory: BootstrapCategory.TARGET_TABLE_MISSING,
      tableCount: 0,
      tables: {}
    }
  };
  Object.entries(xClusterConfigNeedBootstrapPerTableResponse).forEach(
    ([tableUuid, needBootstrapDetails]) => {
      const { reasons, bootstrapRequired } = needBootstrapDetails;
      const isBidirectionalReplicationParticipant = reasons.includes(
        XClusterNeedBootstrapReason.BIDIRECTIONAL_REPLICATION
      );
      const tableHasData = reasons.includes(XClusterNeedBootstrapReason.TABLE_HAS_DATA);
      const targetTableMissing = reasons.includes(
        XClusterNeedBootstrapReason.TABLE_MISSING_ON_TARGET
      );

      if (bootstrapRequired) {
        categorizedNeedBootstrapPerTableResponse.bootstrapTableUuids.push(tableUuid);
      }

      // In the following assignments, we won't be writing over an existing entries because YBA
      // backend returns an entry per tableUuid. i.e. `.tables[tableUuid]` is always undefined.
      if (isBidirectionalReplicationParticipant && tableHasData) {
        categorizedNeedBootstrapPerTableResponse.tableHasDataBidirectional.tables[
          tableUuid
        ] = needBootstrapDetails;
        categorizedNeedBootstrapPerTableResponse.tableHasDataBidirectional.tableCount += 1;
      } else if (isBidirectionalReplicationParticipant && targetTableMissing) {
        categorizedNeedBootstrapPerTableResponse.targetTableMissingBidirectional.tables[
          tableUuid
        ] = needBootstrapDetails;
        categorizedNeedBootstrapPerTableResponse.targetTableMissingBidirectional.tableCount += 1;
      } else if (tableHasData) {
        categorizedNeedBootstrapPerTableResponse.tableHasData.tables[
          tableUuid
        ] = needBootstrapDetails;
        categorizedNeedBootstrapPerTableResponse.tableHasData.tableCount += 1;
      } else if (targetTableMissing) {
        categorizedNeedBootstrapPerTableResponse.targetTableMissing.tables[
          tableUuid
        ] = needBootstrapDetails;
        categorizedNeedBootstrapPerTableResponse.targetTableMissing.tableCount += 1;
      } else {
        categorizedNeedBootstrapPerTableResponse.noBootstrapRequired.tables[
          tableUuid
        ] = needBootstrapDetails;
        categorizedNeedBootstrapPerTableResponse.noBootstrapRequired.tableCount += 1;
      }
    }
  );
  return categorizedNeedBootstrapPerTableResponse;
};

export const getMasterNodeAddress = (nodeDetailsSet: Array<any>) => {
  const master = nodeDetailsSet.find((node: Record<string, any>) => node.isMaster);
  if (master) {
    return master.cloudInfo.private_ip + ':' + master.masterRpcPort;
  }
  return '';
};

export const convertToLocalTime = (time: string, timezone: string | undefined) => {
  return (timezone ? (moment.utc(time) as any).tz(timezone) : moment.utc(time).local()).format(
    'YYYY-MM-DD H:mm:ss'
  );
};

export const formatBytes = function (sizeInBytes: any) {
  if (Number.isInteger(sizeInBytes)) {
    const bytes = sizeInBytes;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB'];
    const k = 1024;
    if (bytes <= 0) {
      return bytes + ' ' + sizes[0];
    }

    const sizeIndex = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, sizeIndex)).toFixed(2)) + ' ' + sizes[sizeIndex];
  } else {
    return '-';
  }
};

/**
 * Returns an object containing the start and end moment for a given time range option.
 */
export const getMetricTimeRange = (
  metricTimeRangeOption: StandardMetricTimeRangeOption
): MetricTimeRange => ({
  startMoment: moment().subtract(metricTimeRangeOption.value, metricTimeRangeOption.type),
  endMoment: moment()
});

/**
 * Wraps parseFloat and lets undefined and number type values pass through.
 */
export const parseFloatIfDefined = (input: string | number | undefined) => {
  if (typeof input === 'number' || input === undefined) {
    return input;
  }
  return parseFloat(input);
};

export const getEnabledConfigActions = (
  replication: XClusterConfig,
  sourceUniverse: Universe | undefined,
  targetUniverse: Universe | undefined,
  isXClusterConfigAllBidirectional: boolean,
  drConfigState?: DrConfigState
): XClusterConfigAction[] => {
  if (drConfigState === DrConfigState.ERROR) {
    // When DR config is in error state, we only allow the DR config delete operation.
    return [];
  }

  if (
    UnavailableUniverseStates.includes(getUniverseStatus(sourceUniverse).state) ||
    UnavailableUniverseStates.includes(getUniverseStatus(targetUniverse).state)
  ) {
    // xCluster 'Delete' action will fail on the backend. But if the user selects the
    // 'force delete' option, then they will be able to remove the config even if a
    // participating universe is unavailable.
    return [XClusterConfigAction.DELETE];
  }

  const getEnabledConfigActionsBasedOnStatus = (
    status: XClusterConfigStatus
  ): XClusterConfigAction[] => {
    switch (status) {
      case XClusterConfigStatus.INITIALIZED:
      case XClusterConfigStatus.UPDATING:
        return [XClusterConfigAction.DELETE, XClusterConfigAction.RESTART];
      case XClusterConfigStatus.RUNNING:
        return [
          replication.paused ? XClusterConfigAction.RESUME : XClusterConfigAction.PAUSE,
          XClusterConfigAction.MANAGE_TABLE,
          XClusterConfigAction.DB_SYNC,
          XClusterConfigAction.DELETE,
          XClusterConfigAction.EDIT,
          XClusterConfigAction.RESTART
        ];
      case XClusterConfigStatus.FAILED:
        return [XClusterConfigAction.DELETE, XClusterConfigAction.RESTART];
      case XClusterConfigStatus.DELETED_UNIVERSE:
      case XClusterConfigStatus.DELETION_FAILED:
        return [XClusterConfigAction.DELETE];
      default:
        return assertUnreachableCase(status);
    }
  };

  const enabledActions = getEnabledConfigActionsBasedOnStatus(replication.status);

  // Remove `RESTART` action if all tables in the config are in bidirectional replication.
  return isXClusterConfigAllBidirectional
    ? enabledActions.filter((action) => action !== XClusterConfigAction.RESTART)
    : enabledActions;
};

/**
 * Returns the UUIDs for all xCluster configs associated with the provided universe.
 */
export const getXClusterConfigUuids = (universe: Universe | undefined) => ({
  sourceXClusterConfigUuids: universe?.universeDetails?.xclusterInfo?.sourceXClusterConfigs ?? [],
  targetXClusterConfigUuids: universe?.universeDetails?.xclusterInfo?.targetXClusterConfigs ?? []
});

export const hasLinkedXClusterConfig = (universes: Universe[]) =>
  universes.some((universe) => {
    const { sourceXClusterConfigUuids, targetXClusterConfigUuids } = getXClusterConfigUuids(
      universe
    );
    return sourceXClusterConfigUuids.length > 0 || targetXClusterConfigUuids.length > 0;
  });

/**
 * Returns the UUIDs for all xCluster configs with the provided source and target universe.
 */
export const getSharedXClusterConfigs = (sourceUniverse: Universe, targetUniverse: Universe) => {
  const sourceXClusterConfigs = sourceUniverse.universeDetails?.xclusterInfo?.sourceXClusterConfigs;
  const targetXClusterConfigs = targetUniverse.universeDetails?.xclusterInfo?.targetXClusterConfigs;

  const targetUniverseConfigUUIDs = new Set(targetXClusterConfigs);
  return sourceXClusterConfigs
    ? sourceXClusterConfigs.filter((configUUID) => targetUniverseConfigUUIDs.has(configUUID))
    : [];
};

/**
 * Adapt UUID to the format required for xCluster work.
 * - UUIDs are generally given in XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX format
 * - UUIDs used in xCluster endpoints often have the '-' stripped away
 */
export const formatUuidForXCluster = (tableUuid: string) => tableUuid.replaceAll('-', '');

/**
 * Adapt UUID from format required for xCluster work to the common format with dashes.
 * - UUIDs are generally given in XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX format
 * - UUIDs used in xCluster endpoints often have the '-' stripped away
 */
export const formatUuidFromXCluster = (tableUuid: string) =>
  tableUuid.replace(
    /^([0-9a-f]{8})([0-9a-f]{4})([0-9a-f]{4})([0-9a-f]{4})([0-9a-f]{12})$/,
    '$1-$2-$3-$4-$5'
  );

export const getStrictestReplicationLagAlertConfig = (
  alertConfigs: AlertConfiguration[] | undefined
): AlertConfiguration | undefined =>
  alertConfigs?.reduce(
    (
      strictestReplicationLagAlertConfig: AlertConfiguration | undefined,
      currentReplicationLagAlertConfig
    ) => {
      const isUpperLimitThreshold =
        currentReplicationLagAlertConfig.thresholds.SEVERE?.condition ===
        AlertThresholdCondition.GREATER_THAN;
      const isReplicationLagAlert =
        currentReplicationLagAlertConfig.template === AlertTemplate.REPLICATION_LAG;
      const strictestThreshold = strictestReplicationLagAlertConfig?.thresholds.SEVERE?.threshold;
      const currentThreshold = currentReplicationLagAlertConfig.thresholds.SEVERE?.threshold;

      return strictestThreshold &&
        (!currentThreshold ||
          !isUpperLimitThreshold ||
          !isReplicationLagAlert ||
          strictestThreshold <= currentThreshold)
        ? strictestReplicationLagAlertConfig
        : isUpperLimitThreshold && isReplicationLagAlert
        ? currentReplicationLagAlertConfig
        : undefined;
    },
    undefined
  );

/**
 * Returns undefined when `alertConfigs` is undefined or empty array.
 */
export const getStrictestReplicationLagAlertThreshold = (
  alertConfigs: AlertConfiguration[] | undefined
): number | undefined =>
  getStrictestReplicationLagAlertConfig(alertConfigs)?.thresholds?.SEVERE?.threshold;

export const tableSort = <RowType,>(
  a: RowType,
  b: RowType,
  sortField: keyof RowType,
  sortOrder: SortOrder,
  tieBreakerField: keyof RowType
) => {
  let ord = 0;

  ord = a[sortField] < b[sortField] ? -1 : 1;
  // Break ties with the provided tiebreaker field in ascending order.
  if (a[sortField] === b[sortField]) {
    return a[tieBreakerField] < b[tieBreakerField] ? -1 : 1;
  }

  return sortOrder === SortOrder.ASCENDING ? ord : ord * -1;
};

/**
 * Return the table type (YSQL or YCQL) of an xCluster config.
 */
export const getXClusterConfigTableType = (
  xClusterConfig: XClusterConfig
): XClusterTableType | null => {
  // We allow undefined sourceUniverseTables because we are still able to return a value as long as
  // the xCluster config has updated its internal table type field.

  switch (xClusterConfig.tableType) {
    case 'YSQL':
      return TableType.PGSQL_TABLE_TYPE;
    case 'YCQL':
      return TableType.YQL_TABLE_TYPE;
    case 'UNKNOWN':
      return (
        (xClusterConfig.tableDetails?.find((tableDetail) => tableDetail.sourceTableInfo?.tableType)
          ?.sourceTableInfo?.tableType as XClusterTableType) ?? null
      );
  }
};

/**
 * Returns whether the provided table can be added/removed from the xCluster config.
 */
export const isTableToggleable = (
  table: MainTableReplicationCandidate | IndexTableReplicationCandidate,
  xClusterConfigAction: XClusterConfigAction
) =>
  table.eligibilityDetails.status === XClusterTableEligibility.ELIGIBLE_UNUSED ||
  (xClusterConfigAction === XClusterConfigAction.MANAGE_TABLE &&
    table.eligibilityDetails.status === XClusterTableEligibility.ELIGIBLE_IN_CURRENT_CONFIG);

export const shouldAutoIncludeIndexTables = (xClusterConfig: XClusterConfig | undefined) =>
  xClusterConfig ? xClusterConfig.type === XClusterConfigType.TXN : true;

export const getIsTransactionalAtomicityEnabled = (
  sourceUniverse: Universe,
  targetUniverse?: Universe
) => {
  const ybSoftwareVersion = getPrimaryCluster(sourceUniverse.universeDetails.clusters)?.userIntent
    .ybSoftwareVersion;
  const participatingUniverses = targetUniverse
    ? [sourceUniverse, targetUniverse]
    : [sourceUniverse];
  const participantsHaveLinkedXClusterConfig = hasLinkedXClusterConfig(participatingUniverses);
  return (
    !!ybSoftwareVersion &&
    compareYBSoftwareVersions({
      versionA: TRANSACTIONAL_ATOMICITY_YB_SOFTWARE_VERSION_THRESHOLD,
      versionB: ybSoftwareVersion,
      options: {
        suppressFormatError: true
      }
    }) < 0 &&
    !participantsHaveLinkedXClusterConfig
  );
};

/**
 * Returns a string identifier for a given namespace using fields provided from the
 * `GET /customers/{customerUuid}/universes/{universeUuid}/namespaces` endpoint.
 */
export const getNamespaceIdentifier = (namespaceName: string, tableType: string) =>
  `${namespaceName}-${tableType}`;

/**
 * Returns a map keyed by `<namespaceName>-<namespaceTableType>`.
 */
export const getNamespaceIdentifierToNamespaceUuidMap = (
  namespaces: UniverseNamespace[]
): { [namespaceName: string]: string } =>
  // It is important to add tableType to the key because it is possible
  // to have a YSQL namespace and a YCQL namespace with the same name.
  Object.fromEntries(
    namespaces.map((namespace) => [
      getNamespaceIdentifier(namespace.name, namespace.tableType),
      namespace.namespaceUUID
    ])
  );

/**
 * Returns a map of table UUIDs to table details for all tables which are part of the replication config.
 */
export const getInConfigTableUuidsToTableDetailsMap = (tableDetails: XClusterTableDetails[]) =>
  tableDetails.reduce((tableUuidToTableDetails, tableDetails) => {
    if (!UNCONFIGURED_XCLUSTER_TABLE_STATUSES.includes(tableDetails.status)) {
      tableUuidToTableDetails.set(tableDetails.tableId, tableDetails);
    }
    return tableUuidToTableDetails;
  }, new Map<string, XClusterTableDetails>());

/**
 * Filters out the extra table details and returns the table UUIDs for only tables in
 * the replication config.
 */
export const getInConfigTableUuid = (tableDetails: XClusterTableDetails[]) =>
  tableDetails.reduce((inConfigTableUuids: string[], tableDetails) => {
    if (!UNCONFIGURED_XCLUSTER_TABLE_STATUSES.includes(tableDetails.status)) {
      inConfigTableUuids.push(tableDetails.tableId);
    }
    return inConfigTableUuids;
  }, []);

/**
 * Accepts the backend need bootstrap response.
 * Returns true if every table in the xCluster config is part of a database under bidirectional replication.
 */
export const getIsXClusterConfigAllBidirectional = (
  xClusterConfigNeedBootstrapPerTableResponse: XClusterConfigNeedBootstrapPerTableResponse
): boolean => {
  return Object.entries(
    xClusterConfigNeedBootstrapPerTableResponse
  ).every(([_, needBootstrapDetails]) =>
    needBootstrapDetails.reasons.includes(XClusterNeedBootstrapReason.BIDIRECTIONAL_REPLICATION)
  );
};

const updateTableStatusWithReplicationLag = (
  tableStatus: XClusterTableStatus,
  maxAcceptableLag: number | undefined,
  replicationLag: number | undefined
) =>
  tableStatus === XClusterTableStatus.RUNNING &&
  maxAcceptableLag &&
  replicationLag &&
  replicationLag > maxAcceptableLag
    ? XClusterTableStatus.WARNING
    : tableStatus;

/**
 * Returns array of XClusterReplicationTable or array of XClusterTable by augmenting YBTable with XClusterTableDetails.
 * - XClusterReplicationTable: may contain dropped tables
 * - XClusterTable: doest not contain dropped tables
 */
export const augmentTablesWithXClusterDetails = <TIncludeDroppedTables extends boolean>(
  xClusterConfigTables: XClusterTableDetails[],
  maxAcceptableLag: number | undefined,
  metricTraces: MetricTrace[] | undefined,

  options?: {
    includeUnconfiguredTables: boolean;
    includeDroppedTables: TIncludeDroppedTables;
  }
): TIncludeDroppedTables extends true ? XClusterReplicationTable[] : XClusterTable[] => {
  const tableIdToReplicationLag = new Map<string, number | undefined>(
    // Casting `trace.tableId` as string because we currently don't have specific types for each possible metric trace.
    // Metric trace with table level replication lag will have a string tableId provided.
    metricTraces?.map((trace) => [
      trace.tableId as string,
      parseFloatIfDefined(trace.y[trace.y.length - 1])
    ])
  );

  // Augment tables in the current xCluster config with additional table details for the YBA UI.
  const tables = xClusterConfigTables.reduce((tables: XClusterReplicationTable[], tableDetails) => {
    const { tableId, sourceTableInfo, targetTableInfo, ...xClusterTableDetails } = tableDetails;
    const replicationLag =
      tableIdToReplicationLag.get(tableId) ?? XCLUSTER_UNDEFINED_LAG_NUMERIC_REPRESENTATION;
    const tableStatus = updateTableStatusWithReplicationLag(
      xClusterTableDetails.status,
      maxAcceptableLag,
      replicationLag
    );

    if (
      UNCONFIGURED_XCLUSTER_TABLE_STATUSES.includes(tableStatus) &&
      !options?.includeUnconfiguredTables
    ) {
      return tables;
    }

    if (DROPPED_XCLUSTER_TABLE_STATUSES.includes(tableStatus) && !options?.includeDroppedTables) {
      return tables;
    }

    if (sourceTableInfo) {
      tables.push({
        ...sourceTableInfo,
        ...xClusterTableDetails,
        status: tableStatus,
        statusLabel: i18n.t(`${I18N_KEY_PREFIX_XCLUSTER_TABLE_STATUS}.${tableStatus}`),
        replicationLag
      });
    } else if (targetTableInfo) {
      tables.push({
        ...targetTableInfo,
        ...xClusterTableDetails,
        status: tableStatus,
        statusLabel: i18n.t(`${I18N_KEY_PREFIX_XCLUSTER_TABLE_STATUS}.${tableStatus}`),
        replicationLag
      });
    } else {
      // Table dropped from both source and target.
      // YBA backend does not provide a status for this case. Thus, on the client side we will
      // use `XClusterTableStatus.DROPPED` to indicate no table detail information is available.
      tables.push({
        ...xClusterTableDetails,
        tableUUID: tableId,
        status: XClusterTableStatus.DROPPED,
        statusLabel: i18n.t(
          `${I18N_KEY_PREFIX_XCLUSTER_TABLE_STATUS}.${XClusterTableStatus.DROPPED}`
        ),
        replicationLag
      });
    }

    return tables;
  }, []);

  return tables as TIncludeDroppedTables extends true
    ? XClusterReplicationTable[]
    : XClusterTable[];
};
