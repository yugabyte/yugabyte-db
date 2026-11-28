import { http, HttpResponse } from 'msw';

import {
  XClusterConfigStatus,
  XClusterConfigType,
  XClusterTableStatus
} from '@app/components/xcluster/constants';
import { DrConfig, DrConfigState } from '@app/components/xcluster/disasterRecovery/dtos';
import {
  XClusterConfig,
  XClusterReplicationStatusError,
  XClusterTableDetails
} from '@app/components/xcluster/dtos';
import {
  AlertTemplate,
  AlertThresholdCondition,
  IAlertConfiguration
} from '@app/redesign/features/alerts/TemplateComposer/ICustomVariables';
import { MetricsQueryResponse, Universe } from '@app/redesign/helpers/dtos';

export const XCLUSTER_MOCK_CONFIG_UUID = 'b1000001-0001-4000-8000-000000000001';
export const DR_MOCK_CONFIG_UUID = 'b1000001-0001-4000-8000-000000000002';
export const XCLUSTER_MOCK_SOURCE_UNIVERSE_UUID = 'b1000001-0001-4000-8000-000000000003';
export const XCLUSTER_MOCK_TARGET_UNIVERSE_UUID = 'b1000001-0001-4000-8000-000000000004';

// Matches any API origin so the handlers work in Storybook (local backend URL) and in vitest
// (relative `/api/v1`).
const V1_CUSTOMER_ROOT = '*/api/v1/customers/:customerUuid';

export const mockTableDetails = (
  tableId: string,
  status: XClusterTableStatus,
  replicationStatusErrors: XClusterReplicationStatusError[] = []
): XClusterTableDetails => ({
  tableId,
  status,
  replicationStatusErrors,
  indexTable: false,
  needBootstrap: false,
  replicationSetupDone: true,
  streamId: `stream-${tableId}`
});

export const HEALTHY_TABLES = [
  mockTableDetails('table-1', XClusterTableStatus.RUNNING),
  mockTableDetails('table-2', XClusterTableStatus.RUNNING)
];

const mockXClusterConfig = (tableDetails: XClusterTableDetails[]): XClusterConfig => ({
  uuid: XCLUSTER_MOCK_CONFIG_UUID,
  name: 'mock-xcluster-config',
  automaticDdlMode: false,
  createTime: '2026-01-01T00:00:00Z',
  modifyTime: '2026-01-01T00:00:00Z',
  paused: false,
  pitrConfigs: [],
  replicationGroupName: 'mock-replication-group',
  sourceActive: true,
  targetActive: true,
  status: XClusterConfigStatus.RUNNING,
  tableDetails,
  tableType: 'YSQL',
  tables: tableDetails.map((table) => table.tableId),
  type: XClusterConfigType.TXN,
  usedForDr: false,
  sourceUniverseUUID: XCLUSTER_MOCK_SOURCE_UNIVERSE_UUID,
  targetUniverseUUID: XCLUSTER_MOCK_TARGET_UNIVERSE_UUID
});

const mockDrConfig = (tableDetails: XClusterTableDetails[], state: DrConfigState): DrConfig =>
  ({
    uuid: DR_MOCK_CONFIG_UUID,
    name: 'mock-dr-config',
    createTime: '2026-01-01T00:00:00Z',
    modifyTime: '2026-01-01T00:00:00Z',
    state,
    automaticDdlMode: false,
    paused: false,
    pitrConfigs: [],
    pitrRetentionPeriodSec: 86400,
    pitrSnapshotIntervalSec: 3600,
    replicationGroupName: 'mock-replication-group',
    status: XClusterConfigStatus.RUNNING,
    tableDetails,
    tables: tableDetails.map((table) => table.tableId),
    type: XClusterConfigType.TXN,
    primaryUniverseActive: true,
    primaryUniverseUuid: XCLUSTER_MOCK_SOURCE_UNIVERSE_UUID,
    drReplicaUniverseActive: true,
    drReplicaUniverseUuid: XCLUSTER_MOCK_TARGET_UNIVERSE_UUID,
    xclusterConfigUuid: XCLUSTER_MOCK_CONFIG_UUID,
    xclusterConfigsUuid: [XCLUSTER_MOCK_CONFIG_UUID]
  }) as unknown as DrConfig;

// The banner only reads the UUID and node prefix from the source universe.
const mockSourceUniverse = {
  universeUUID: XCLUSTER_MOCK_SOURCE_UNIVERSE_UUID,
  name: 'mock-source-universe',
  universeDetails: { nodePrefix: 'yb-mock-source' }
} as unknown as Universe;

const mockReplicationLagAlertConfig = (severeThresholdMs: number): IAlertConfiguration => ({
  uuid: 'b1000001-0001-4000-8000-000000000005',
  customerUUID: 'customer-uuid',
  name: 'Replication Lag',
  description: 'Average replication lag for 10 minutes in ms is above threshold',
  createTime: '2026-01-01T00:00:00Z',
  targetType: 'UNIVERSE',
  target: { all: true },
  thresholds: {
    SEVERE: { condition: AlertThresholdCondition.GREATER_THAN, threshold: severeThresholdMs }
  },
  thresholdUnit: 'MILLISECOND',
  template: AlertTemplate.REPLICATION_LAG,
  durationSec: 600,
  active: true,
  defaultDestination: true
});

// The metrics API returns trace values as strings.
const mockReplicationLagMetrics = (latestLagMsPerTable: number[]): MetricsQueryResponse =>
  ({
    async_replication_sent_lag: {
      data: latestLagMsPerTable.map((latestLagMs, index) => ({
        metricName: 'async_replication_sent_lag',
        name: `table-${index + 1}`,
        tableId: `table-${index + 1}`,
        type: 'scatter',
        x: [1767225600000, 1767225660000],
        y: ['10', String(latestLagMs)]
      })),
      directURLs: [],
      layout: {},
      metricsLinkUseBrowserFqdn: false,
      queryKey: 'async_replication_sent_lag'
    }
  }) as unknown as MetricsQueryResponse;

export interface XClusterBannerMockScenario {
  tableDetails: XClusterTableDetails[];
  // Severe threshold of the source universe's replication lag alert. `null` means no alert is
  // configured.
  replicationLagAlertThresholdMs: number | null;
  // Latest replication lag sample for each table in the metrics response.
  latestLagMsPerTable: number[];
  // Fails the alert config and metrics requests, which the banner should treat as "no lag data".
  lagRequestsFail?: boolean;
  drState?: DrConfigState;
}

/**
 * MSW handlers serving an xCluster config, a DR config, the source universe, the replication lag
 * alert config, and the replication lag metrics, all built from one scenario.
 */
export const xClusterBannerMswHandlers = (scenario: XClusterBannerMockScenario) => {
  const lagFailure = () =>
    HttpResponse.json({ success: false, error: 'Internal server error' }, { status: 500 });
  return [
    http.get(`${V1_CUSTOMER_ROOT}/xcluster_configs/${XCLUSTER_MOCK_CONFIG_UUID}`, () =>
      HttpResponse.json(mockXClusterConfig(scenario.tableDetails))
    ),
    http.get(`${V1_CUSTOMER_ROOT}/dr_configs/${DR_MOCK_CONFIG_UUID}`, () =>
      HttpResponse.json(
        mockDrConfig(scenario.tableDetails, scenario.drState ?? DrConfigState.REPLICATING)
      )
    ),
    http.get(`${V1_CUSTOMER_ROOT}/universes/${XCLUSTER_MOCK_SOURCE_UNIVERSE_UUID}`, () =>
      HttpResponse.json(mockSourceUniverse)
    ),
    http.post(`${V1_CUSTOMER_ROOT}/alert_configurations/list`, () => {
      if (scenario.lagRequestsFail) {
        return lagFailure();
      }
      return HttpResponse.json(
        scenario.replicationLagAlertThresholdMs === null
          ? []
          : [mockReplicationLagAlertConfig(scenario.replicationLagAlertThresholdMs)]
      );
    }),
    http.post(`${V1_CUSTOMER_ROOT}/metrics`, () =>
      scenario.lagRequestsFail
        ? lagFailure()
        : HttpResponse.json(mockReplicationLagMetrics(scenario.latestLagMsPerTable))
    )
  ];
};

const LAG_THRESHOLD_MS = 180000;
const LAG_BELOW_THRESHOLD_MS = 1200;
const LAG_ABOVE_THRESHOLD_MS = 240000;

/**
 * Representative API states. Each one exercises a distinct path from API response to banner
 * (none / warning / error without restart / error with restart).
 */
export const XCLUSTER_BANNER_SCENARIOS = {
  healthy: {
    tableDetails: HEALTHY_TABLES,
    replicationLagAlertThresholdMs: LAG_THRESHOLD_MS,
    latestLagMsPerTable: [LAG_BELOW_THRESHOLD_MS, LAG_BELOW_THRESHOLD_MS]
  },
  // Statuses that are not problems (transitional or warning-level) must not raise the banner.
  nonProblemStatuses: {
    tableDetails: [
      mockTableDetails('table-1', XClusterTableStatus.BOOTSTRAPPING),
      mockTableDetails('table-2', XClusterTableStatus.UPDATING),
      mockTableDetails('table-3', XClusterTableStatus.WARNING)
    ],
    replicationLagAlertThresholdMs: LAG_THRESHOLD_MS,
    latestLagMsPerTable: [LAG_BELOW_THRESHOLD_MS, LAG_BELOW_THRESHOLD_MS, LAG_BELOW_THRESHOLD_MS]
  },
  lagAboveThreshold: {
    tableDetails: HEALTHY_TABLES,
    replicationLagAlertThresholdMs: LAG_THRESHOLD_MS,
    latestLagMsPerTable: [LAG_BELOW_THRESHOLD_MS, LAG_ABOVE_THRESHOLD_MS]
  },
  // High lag with no replication lag alert configured: there is no threshold to exceed.
  lagWithoutAlertConfig: {
    tableDetails: HEALTHY_TABLES,
    replicationLagAlertThresholdMs: null,
    latestLagMsPerTable: [LAG_ABOVE_THRESHOLD_MS, LAG_ABOVE_THRESHOLD_MS]
  },
  lagRequestsFail: {
    tableDetails: HEALTHY_TABLES,
    replicationLagAlertThresholdMs: LAG_THRESHOLD_MS,
    latestLagMsPerTable: [],
    lagRequestsFail: true
  },
  // A table being created on the source in automatic DDL mode: transient, so no banner.
  uninitializedTableOnly: {
    tableDetails: [
      ...HEALTHY_TABLES,
      mockTableDetails('table-3', XClusterTableStatus.ERROR, [
        XClusterReplicationStatusError.ERROR_UNINITIALIZED
      ])
    ],
    replicationLagAlertThresholdMs: LAG_THRESHOLD_MS,
    latestLagMsPerTable: [LAG_BELOW_THRESHOLD_MS, LAG_BELOW_THRESHOLD_MS]
  },
  // DDL queue in system error while a new table waits uninitialized behind it.
  systemErrorWithUninitializedTable: {
    tableDetails: [
      ...HEALTHY_TABLES,
      mockTableDetails('ddl_queue', XClusterTableStatus.ERROR, [
        XClusterReplicationStatusError.SYSTEM_ERROR
      ]),
      mockTableDetails('table-3', XClusterTableStatus.ERROR, [
        XClusterReplicationStatusError.ERROR_UNINITIALIZED
      ])
    ],
    replicationLagAlertThresholdMs: LAG_THRESHOLD_MS,
    latestLagMsPerTable: [LAG_BELOW_THRESHOLD_MS, LAG_BELOW_THRESHOLD_MS]
  },
  // One table per problem group except Missing Op ID.
  problemTablesWithoutMissingOp: {
    tableDetails: [
      mockTableDetails('table-1', XClusterTableStatus.ERROR, [
        XClusterReplicationStatusError.SCHEMA_MISMATCH
      ]),
      mockTableDetails('table-2', XClusterTableStatus.ERROR, [
        XClusterReplicationStatusError.AUTO_FLAG_CONFIG_MISMATCH
      ]),
      mockTableDetails('table-3', XClusterTableStatus.ERROR),
      mockTableDetails('table-4', XClusterTableStatus.UNABLE_TO_FETCH),
      mockTableDetails('table-5', XClusterTableStatus.FAILED),
      mockTableDetails('table-6', XClusterTableStatus.EXTRA_TABLE_ON_SOURCE),
      mockTableDetails('table-7', XClusterTableStatus.ERROR, [
        XClusterReplicationStatusError.SOURCE_UNREACHABLE
      ])
    ],
    replicationLagAlertThresholdMs: LAG_THRESHOLD_MS,
    latestLagMsPerTable: [LAG_BELOW_THRESHOLD_MS]
  },
  missingOp: {
    tableDetails: [
      mockTableDetails('table-1', XClusterTableStatus.RUNNING),
      mockTableDetails('table-2', XClusterTableStatus.ERROR, [
        XClusterReplicationStatusError.MISSING_OP
      ])
    ],
    replicationLagAlertThresholdMs: LAG_THRESHOLD_MS,
    latestLagMsPerTable: [LAG_BELOW_THRESHOLD_MS, LAG_BELOW_THRESHOLD_MS]
  },
  // Missing Op ID on a table that also has another error, plus lag above threshold.
  missingOpWithOtherIssuesAndLag: {
    tableDetails: [
      mockTableDetails('table-1', XClusterTableStatus.ERROR, [
        XClusterReplicationStatusError.MISSING_OP,
        XClusterReplicationStatusError.SCHEMA_MISMATCH
      ]),
      mockTableDetails('table-2', XClusterTableStatus.UNABLE_TO_FETCH)
    ],
    replicationLagAlertThresholdMs: LAG_THRESHOLD_MS,
    latestLagMsPerTable: [LAG_ABOVE_THRESHOLD_MS, LAG_ABOVE_THRESHOLD_MS]
  }
} satisfies Record<string, XClusterBannerMockScenario>;
