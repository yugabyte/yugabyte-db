import { Tab } from 'react-bootstrap';
import { YBTabsPanel } from '../panels';
import { LiveQueries } from './index';
import { SlowQueries } from './SlowQueries';
import { PerfAdvisor } from './PerfAdvisor.tsx';

export const QueriesViewer = (props) => {
  return (
    <div>
      <YBTabsPanel defaultTab={'live-queries'} id="queries-tab-panel">
        <Tab eventKey="live-queries" title="Live Queries" key="live-queries">
          <LiveQueries />
        </Tab>
        <Tab eventKey="slow-queries" title="Slow Queries" key="slow-queries">
          <SlowQueries />
        </Tab>
        {props.isPerfAdvisorEnabled && (
          <Tab
            eventKey="perf-advisor"
            title="Performance Advisor"
            key="perf-advisor"
            unmountOnExit={true}
          >
            <PerfAdvisor />
          </Tab>
        )}
      </YBTabsPanel>
    </div>
  );
};
