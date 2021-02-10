import React from 'react';
import { Tab, Row, Col } from 'react-bootstrap';
import { YBTabsPanel } from '../panels'
import { LiveQueries } from './index';
import { SlowQueries } from './SlowQueries';

export const QueriesViewer = (props) => {
  return (
    <div>
      <YBTabsPanel
        defaultTab={"live-queries"}
        id="queries-tab-panel"
      >
        <Tab eventKey="live-queries" title="Live Queries" key="live-queries">
          <LiveQueries />
        </Tab>
        <Tab eventKey="slow-queries" title="Slow Queries" key="slow-queries">
          <SlowQueries />
        </Tab>
      </YBTabsPanel>      
    </div>
  );
}