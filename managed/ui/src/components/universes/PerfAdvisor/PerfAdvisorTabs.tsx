import { useEffect, useState } from 'react';
import { Tab } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { withRouter } from 'react-router';
import { Box } from '@material-ui/core';
import { PerfAdvisorOverviewDashboard } from '../AttachUniverseToPerfAdvisor/PerfAdvisorOverviewDashboard';
import { YBTabsPanel } from '../../../components/panels';

interface PerfAdvisorTabsProps {
  universeUuid: string;
  timezone: string;
  apiUrl: string;
  registrationStatus: boolean;
  location?: any;
  router?: any;
}

export const ConfigTabKey = {
  CLUSTER_LOAD: 'clusterLoad',
  METRICS: 'metricsNew'
} as const;
export type ConfigTabKey = typeof ConfigTabKey[keyof typeof ConfigTabKey];

const PerfAdvisorTabsComponent = ({
  universeUuid,
  timezone,
  apiUrl,
  registrationStatus,
  location,
  router
}: PerfAdvisorTabsProps) => {
  const { t } = useTranslation();

  // Get tab from URL query params (react-router v3 style)
  const tabFromUrl = (location?.query?.tab as ConfigTabKey) || ConfigTabKey.CLUSTER_LOAD;
  const [tabToDisplay, setTabToDisplay] = useState<ConfigTabKey>(tabFromUrl);

  useEffect(() => {
    // Update state when URL tab changes
    if (location?.query?.tab) {
      setTabToDisplay(location.query.tab as ConfigTabKey);
    }
  }, [location?.query?.tab]);

  // Handle initial tab setup and subtab cleanup
  useEffect(() => {
    if (!router || !location) return;

    const currentTab = location.query?.tab as ConfigTabKey;
    const currentQuery = location.query || {};

    // If no tab is specified in the URL, set default tab with subTab
    if (!currentTab) {
      const currentLocation = { ...location };
      currentLocation.query = {
        ...currentQuery, // Preserve all existing params (duration, etc.)
        tab: ConfigTabKey.CLUSTER_LOAD
      };
      router.replace(currentLocation);
      return;
    }

    // Build new query object, removing subtab params that don't belong to current tab
    const newQuery = { ...currentQuery };
    let needsUpdate = false;

    // Remove subTab if current tab doesn't use it (or if it's not clusterLoad)
    if (currentTab !== ConfigTabKey.CLUSTER_LOAD && currentQuery.subTab) {
      delete newQuery.subTab;
      needsUpdate = true;
    }

    // Update URL if we removed any subtab params
    if (needsUpdate) {
      const currentLocation = { ...location };
      currentLocation.query = newQuery;
      router.replace(currentLocation);
    }
  }, [location?.query?.tab, location?.query?.metricsTab, router, location]);

  const tabConfig = [
    {
      key: ConfigTabKey.CLUSTER_LOAD,
      title: t('clusterDetail.troubleshoot.perfAdvisorClusterLoadTab')
    },
    {
      key: ConfigTabKey.METRICS,
      title: t('clusterDetail.troubleshoot.perfAdvisorMetricsTab')
    }
  ];

  return (
    <Box>
      <YBTabsPanel
        defaultTab={tabToDisplay}
        activeTab={tabToDisplay}
        id="perf-advisor-config-tab-panel"
      >
        {tabConfig.map((tab) => (
          <Tab eventKey={tab.key} title={tab.title} key={tab.key} unmountOnExit={true}>
            <PerfAdvisorOverviewDashboard
              universeUuid={universeUuid}
              timezone={timezone}
              apiUrl={apiUrl}
              registrationStatus={registrationStatus}
            />
          </Tab>
        ))}
      </YBTabsPanel>
    </Box>
  );
};

// Export with withRouter to get location and router props
export const PerfAdvisorTabs = withRouter(PerfAdvisorTabsComponent);
