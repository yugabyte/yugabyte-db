import React from 'react';
import { Tab } from 'react-bootstrap';
import { YBTabsPanel } from '../panels';
import { LiveQueries } from './index';
import { SlowQueries } from './SlowQueries';
// import { PerfAdvisor } from './PerfAdvisor.tsx';

// TODO: Under discussion if we need to have Perf Advisor under queries section
export const QueriesViewer = (props) => {
  console.log('QueriesViewer', props);
  return (
    <div>
      <YBTabsPanel defaultTab={'live-queries'} id="queries-tab-panel">
        <Tab eventKey="live-queries" title="Live Queries" key="live-queries">
          <LiveQueries />
        </Tab>
        <Tab eventKey="slow-queries" title="Slow Queries" key="slow-queries">
          <SlowQueries />
        </Tab>
        {/* <Tab eventKey="perf-advisor" title="Performance Advisor" key="perf-advisor">
          <PerfAdvisor />
        </Tab> */}
      </YBTabsPanel>
    </div>
  );
};
