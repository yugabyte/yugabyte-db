import { useEffect } from 'react';
import { Tab } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { withRouter } from 'react-router';
import { Box, Typography } from '@material-ui/core';
import { PerfAdvisorOverviewDashboard } from '../AttachUniverseToPerfAdvisor/PerfAdvisorOverviewDashboard';
import { PaRegistrationMode } from '../../../redesign/features/PerfAdvisor/api';
import { PERF_ADVISOR_PATH } from '@app/redesign/helpers/constants';
import { isPaDrilldownUrl } from '../AttachUniverseToPerfAdvisor/perfAdvisorUrlHelpers';
import { usePerfAdvisorRuntimeConfigs } from '../AttachUniverseToPerfAdvisor/usePerfAdvisorRuntimeConfigs';
import { YBTabsPanel } from '../../../components/panels';

interface PerfAdvisorTabsProps {
  universeUuid: string;
  timezone: string;
  apiUrl: string;
  registrationStatus: boolean;
  registrationMode?: PaRegistrationMode;
  location?: any;
  router?: any;
}

export const ConfigTabKey = {
  CLUSTER_LOAD: 'clusterLoad',
  METRICS: 'metricsNew',
  DATABASES: 'databases',
  ANOMALIES: 'anomalies'
} as const;
export type ConfigTabKey = (typeof ConfigTabKey)[keyof typeof ConfigTabKey];

const PATH_TAB_KEYS: string[] = [
  ConfigTabKey.CLUSTER_LOAD,
  ConfigTabKey.METRICS,
  ConfigTabKey.DATABASES
];

const asPathTabKey = (value: unknown): ConfigTabKey | undefined =>
  typeof value === 'string' && PATH_TAB_KEYS.includes(value) ? (value as ConfigTabKey) : undefined;

// The tab is the first segment after `/perfAdvisor` (`/perfAdvisor/databases/<db>/tables`).
const getTabFromPath = (pathname: string | undefined): ConfigTabKey | undefined =>
  asPathTabKey(pathname?.match(new RegExp(`/${PERF_ADVISOR_PATH}/([^/?#]+)`))?.[1]);

const PerfAdvisorTabsComponent = ({
  universeUuid,
  timezone,
  apiUrl,
  registrationStatus,
  registrationMode,
  location,
  router
}: PerfAdvisorTabsProps) => {
  const { t } = useTranslation();
  const runtimeConfigs = usePerfAdvisorRuntimeConfigs();

  const perfAdvisorBasePath = `/universes/${universeUuid}/${PERF_ADVISOR_PATH}`;
  // Cluster Load and Metrics read Prometheus, which only advanced mode writes.
  const showClusterLoadAndMetrics = registrationMode === PaRegistrationMode.ADVANCED;
  const tabConfig: { key: ConfigTabKey; title: string }[] = [
    ...(showClusterLoadAndMetrics
      ? [
          {
            key: ConfigTabKey.CLUSTER_LOAD,
            title: t('clusterDetail.troubleshoot.perfAdvisorClusterLoadTab')
          },
          {
            key: ConfigTabKey.METRICS,
            title: t('clusterDetail.troubleshoot.perfAdvisorMetricsTab')
          }
        ]
      : []),
    ...(runtimeConfigs.showDatabasesView
      ? [
          {
            key: ConfigTabKey.DATABASES,
            title: t('clusterDetail.troubleshoot.perfAdvisorDatabasesTab')
          }
        ]
      : [])
  ];
  const defaultTabKey: ConfigTabKey | undefined = tabConfig[0]?.key;
  const isTabShown = (key: ConfigTabKey | undefined): key is ConfigTabKey =>
    tabConfig.some((tab) => tab.key === key);
  const pathTab = getTabFromPath(location?.pathname);
  const tabToDisplay = isTabShown(pathTab) ? pathTab : defaultTabKey;

  // Put the tab in the path and send a link to a hidden tab (Cluster Load / Metrics
  // in basic mode) to the first shown tab. A databases path keeps its
  // `/databases/<db>/tables` suffix. Drilldowns are left alone.
  useEffect(() => {
    // The pane stays mounted while the universe tab fades out, so ignore paths outside PA.
    if (
      !router ||
      !defaultTabKey ||
      !location?.pathname?.startsWith(perfAdvisorBasePath) ||
      isPaDrilldownUrl()
    ) {
      return;
    }
    const { tab, subTab, ...restQuery } = location.query ?? {};
    const currentPathTab = getTabFromPath(location.pathname);
    const queryTab = asPathTabKey(tab);
    const requested = queryTab ?? currentPathTab;
    const nextTab = isTabShown(requested) ? requested : defaultTabKey;
    if (currentPathTab === nextTab && tab === undefined && subTab === undefined) {
      return;
    }
    const pathname =
      currentPathTab === nextTab && (!queryTab || queryTab === currentPathTab)
        ? location.pathname
        : `${perfAdvisorBasePath}/${nextTab}`;
    if (pathname === location.pathname && tab === undefined && subTab === undefined) {
      return;
    }
    router.replace({ pathname, query: restQuery });
  }, [router, location, perfAdvisorBasePath, registrationMode, runtimeConfigs.showDatabasesView]);

  // In online mode this collector scrapes the universe but keeps nothing - every view below reads
  // local data, so they would all render empty. Say where the data went instead.
  if (registrationMode === PaRegistrationMode.ONLINE) {
    return (
      <Box p={4} data-testid="PerfAdvisorTabs-OnlineMode">
        <Typography variant="body1">{t('clusterDetail.troubleshoot.onlineModeTitle')}</Typography>
        <Box mt={1}>
          <Typography variant="body2" color="textSecondary">
            {t('clusterDetail.troubleshoot.onlineModeSubText')}
          </Typography>
        </Box>
      </Box>
    );
  }

  // On a query/anomaly drilldown, hide the horizontal Cluster Load / Metrics
  // tabs and render only the drilldown content.
  if (isPaDrilldownUrl()) {
    return (
      <Box>
        <PerfAdvisorOverviewDashboard
          apiUrl={apiUrl}
          timezone={timezone}
          universeUuid={universeUuid}
          registrationStatus={registrationStatus}
          registrationMode={registrationMode}
        />
      </Box>
    );
  }

  return (
    <Box>
      <YBTabsPanel
        defaultTab={tabToDisplay}
        activeTab={tabToDisplay}
        routePrefix={`${perfAdvisorBasePath}/`}
        id="perf-advisor-config-tab-panel"
      >
        {tabConfig.map((tab) => (
          <Tab eventKey={tab.key} title={tab.title} key={tab.key} unmountOnExit={true}>
            <PerfAdvisorOverviewDashboard
              universeUuid={universeUuid}
              timezone={timezone}
              apiUrl={apiUrl}
              registrationStatus={registrationStatus}
              registrationMode={registrationMode}
            />
          </Tab>
        ))}
      </YBTabsPanel>
    </Box>
  );
};

// Export with withRouter to get location and router props
export const PerfAdvisorTabs = withRouter(PerfAdvisorTabsComponent);
