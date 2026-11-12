import { withRouter } from 'react-router';
import { YBTab, YBTabs } from '@yugabyte-ui-library/core';
import { LiveQueries } from './index.js';
import { SlowQueries } from './SlowQueries';
import { PerfAdvisor } from './PerfAdvisor.tsx';
import './QueriesViewer.scss';

const SLOW_QUERIES_TAB = 'slow-queries';
const LIVE_QUERIES_TAB = 'live-queries';
const RECOMMENDATIONS_TAB = 'perf-advisor';

const QueriesViewerComponent = ({ isLegacyPAEnabled, location, router }) => {
  const tabs = [
    { key: SLOW_QUERIES_TAB, title: 'Slow Queries' },
    { key: LIVE_QUERIES_TAB, title: 'Live Queries' },
    ...(isLegacyPAEnabled ? [{ key: RECOMMENDATIONS_TAB, title: 'Recommendations' }] : [])
  ];
  const queryTab = location?.query?.tab;
  const selectedTab = tabs.some((tab) => tab.key === queryTab) ? queryTab : SLOW_QUERIES_TAB;

  const handleTabChange = (_event, tab) => {
    router.push({ pathname: location.pathname, query: { ...location.query, tab } });
  };

  return (
    <div className="queries-viewer">
      <YBTabs value={selectedTab} onChange={handleTabChange} sx={{ pl: 0 }}>
        {tabs.map((tab) => (
          <YBTab
            key={tab.key}
            value={tab.key}
            label={tab.title}
            data-testid={`QueriesViewer-${tab.key}`}
          />
        ))}
      </YBTabs>
      <div className="queries-viewer__content">
        {selectedTab === SLOW_QUERIES_TAB && <SlowQueries />}
        {selectedTab === LIVE_QUERIES_TAB && <LiveQueries />}
        {selectedTab === RECOMMENDATIONS_TAB && <PerfAdvisor />}
      </div>
    </div>
  );
};

export const QueriesViewer = withRouter(QueriesViewerComponent);
