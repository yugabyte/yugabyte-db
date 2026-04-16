import { useEffect, useState } from 'react';
import { Tab } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { Box } from '@material-ui/core';
import { YBTabsPanel } from '../../../components/panels';
import { RegisterYBAToPerfAdvisor } from '../AttachUniverseToPerfAdvisor/RegisterYBAToPerfAdvisor';
import { AppName } from '../../../redesign/features/PerfAdvisor/PerfAdvisorAnalysisDashboard';

interface PerfAdvisorTabsProps {
  universeUUID: string;
  timezone: string;
}

export const ConfigTabKey = {
  ANOMALIES: 'anomalies',
  QUERIES: 'queries',
  METRICS: 'metricsNew',
  INSIGHTS: 'insights'
} as const;
export type ConfigTabKey = typeof ConfigTabKey[keyof typeof ConfigTabKey];

export const PerfAdvisorTabs = ({ universeUUID, timezone }: PerfAdvisorTabsProps) => {
  // Extract the tab name from the URL query parameter
  const queryParams = new URLSearchParams(window.location.search);
  const tabFromUrl = (queryParams.get('tab') as ConfigTabKey) || ConfigTabKey.ANOMALIES; // Default to ANOMALIES if no tab is specified
  const { t } = useTranslation();

  const [tabToDisplay, setTabToDisplay] = useState<ConfigTabKey>(tabFromUrl);

  useEffect(() => {
    // If no tab is specified in the URL, update the URL to include ?tab=anomalies
    if (!queryParams.get('tab')) {
      const newUrl = `${window.location.pathname}?tab=${ConfigTabKey.ANOMALIES}`;
      window.history.replaceState(null, '', newUrl); // Update the URL without reloading the page
    }
    setTabToDisplay(tabFromUrl);
  }, [tabFromUrl]);

  // Set the ref to true when the component is unmounting
  useEffect(() => {
    // // Cleanup function to remove query parameters on unmount
    return () => {
      const basePath = window.location.pathname.split('?')[0]; // Remove query parameters
      window.history.replaceState(null, '', basePath); // Update the URL without reloading the page
    };
  }, []);

  const tabConfig = [
    {
      key: ConfigTabKey.ANOMALIES,
      title: t('clusterDetail.troubleshoot.perfAdvisorAnomaliesTab')
    },
    {
      key: ConfigTabKey.QUERIES,
      title: t('clusterDetail.troubleshoot.perfAdvisorQueriesTab')
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
            <Box mt={1}>
              <RegisterYBAToPerfAdvisor
                universeUuid={universeUUID}
                appName={AppName.YBA}
                timezone={timezone}
              />
            </Box>
          </Tab>
        ))}
      </YBTabsPanel>
    </Box>
  );
};
