import { FC, ReactNode, useContext } from 'react';
import { withRouter, WithRouterProps } from 'react-router';
import { mui, YBTab, YBTabs } from '@yugabyte-ui-library/core';
import { MetricsCardActionsContext, MetricsCardLoaderContext } from './MetricsCardContext';

const { Box, CircularProgress } = mui;

// Advanced Metrics' loader size, so the card keeps its height while the graphs load.
const cardLoader = (
  <Box
    data-testid="MonitoringMetricsCard-Loading"
    sx={{
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      minHeight: '500px',
      mt: 2
    }}
  >
    <CircularProgress />
  </Box>
);

export interface MetricsCardTab {
  key: string;
  title: string;
  content: ReactNode;
}

interface MonitoringMetricsCardProps {
  tabs: MetricsCardTab[];
  defaultTab: string;
  // Forces a tab, e.g. the single tab of the outlier tables and databases views.
  activeTab?: string;
  // Remounts the tab content when it changes, e.g. on a metric measure change.
  contentKey?: string;
}

const MonitoringMetricsCardComponent: FC<MonitoringMetricsCardProps & WithRouterProps> = ({
  tabs,
  defaultTab,
  activeTab,
  contentKey,
  location,
  router
}) => {
  const actions = useContext(MetricsCardActionsContext);
  const queryTab = location?.query?.tab;
  const selectedKey =
    activeTab ?? (tabs.some((tab) => tab.key === queryTab) ? queryTab : defaultTab);
  const selectedTab = tabs.find((tab) => tab.key === selectedKey) ?? tabs[0];

  const handleTabChange = (_event: unknown, tab: string) => {
    router.push({ ...location, query: { ...location.query, tab } });
  };

  return (
    <Box
      data-testid="MonitoringMetricsCard"
      sx={(theme) => ({
        mt: 2,
        px: 2,
        pb: 2,
        backgroundColor: theme.palette.common.white,
        border: `1px solid ${theme.palette.grey[300]}`,
        borderRadius: '8px'
      })}
    >
      {tabs.length > 0 && (
        <YBTabs value={selectedTab?.key} onChange={handleTabChange} scrollable tabGap={0}>
          {tabs.map((tab) => (
            <YBTab
              key={tab.key}
              value={tab.key}
              label={tab.title}
              data-testid={`MonitoringMetricsCard-${tab.key}`}
              // Advanced Metrics' tab spacing; the tab's own padding needs overriding.
              sx={{
                minWidth: 'fit-content',
                paddingLeft: '16px !important',
                paddingRight: '16px !important',
                marginRight: '8px !important'
              }}
            />
          ))}
        </YBTabs>
      )}
      {actions && (
        <Box
          sx={{ display: 'flex', alignItems: 'center', justifyContent: 'flex-end', gap: 1, my: 2 }}
        >
          {actions}
        </Box>
      )}
      <MetricsCardLoaderContext.Provider value={cardLoader}>
        <Box key={`${selectedTab?.key}-${contentKey ?? ''}`}>{selectedTab?.content}</Box>
      </MetricsCardLoaderContext.Provider>
    </Box>
  );
};

export const MonitoringMetricsCard = withRouter(MonitoringMetricsCardComponent);
