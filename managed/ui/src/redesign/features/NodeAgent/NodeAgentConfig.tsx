import React, { FC } from 'react';
import { Tab } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { NodeAgentHeader } from './NodeAgentHeader';
import { YBTabsWithLinksPanel } from '../../../components/panels';
import { Typography } from '@material-ui/core';

export const NODE_AGENT_TABS = {
  AssignedNodes: 'assignedNodes',
  UnassignedNodes: 'unassignedNodes'
};

export const NodeAgentConfig: FC<any> = () => {
  const { t } = useTranslation();

  return (
    <div>
      <Typography variant="h2" className="content-title">
        {t('nodeAgent.title')}
      </Typography>
      <YBTabsWithLinksPanel
        id="node-agent-config"
        defaultTab={NODE_AGENT_TABS.AssignedNodes}
        className="universe-detail data-center-config-tab"
      >
        <Tab
          eventKey={NODE_AGENT_TABS.AssignedNodes}
          title={t('nodeAgent.assignedNodesTitle')}
          unmountOnExit
        >
          <NodeAgentHeader tabKey={NODE_AGENT_TABS.AssignedNodes} />
        </Tab>
        <Tab
          eventKey={NODE_AGENT_TABS.UnassignedNodes}
          title={t('nodeAgent.unassignedNodesTitle')}
          unmountOnExit
        >
          <NodeAgentHeader tabKey={NODE_AGENT_TABS.UnassignedNodes} />
        </Tab>
      </YBTabsWithLinksPanel>
    </div>
  );
};
