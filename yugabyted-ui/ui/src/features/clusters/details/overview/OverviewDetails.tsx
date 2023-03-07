import React, { ComponentType, FC } from 'react';
import { Box, makeStyles, Tab, Tabs } from '@material-ui/core';
import { OverviewTab } from '@app/features/clusters/details/overview/OverviewTab';
import { NodesTab } from '../nodes/NodesTab';
import { generatePath, Link, Route, Switch, useRouteMatch } from 'react-router-dom';
import { useTranslation } from 'react-i18next';
import { ActivityTab } from '../activities/ActivityTab';
import { SettingsTab } from '../settings/SettingsTab';

const useStyles = makeStyles((theme) => ({
  tabSectionContainer: {
    display: 'flex',
    alignItems: 'center',
    width: '100%',
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`
  },
}));

export interface ITabListItem {
  name: string;
  component: ComponentType;
  testId: string;
}

const tabList: ITabListItem[] = [
  {
    name: 'tabOverview',
    component: OverviewTab,
    testId: 'ClusterTabList-Overview'
  },
  {
    name: 'tabNodes',
    component: NodesTab,
    testId: 'ClusterTabList-Nodes'
  },
  {
    name: 'tabActivity',
    component: ActivityTab,
    testId: 'ClusterTabList-Activity'
  },
  {
    name: 'tabSettings',
    component: SettingsTab,
    testId: 'ClusterTabList-Settings'
  },
];

export const OverviewDetails: FC = () => {
  const { path, params } = useRouteMatch<App.RouteParams>();
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Box>
      <div className={classes.tabSectionContainer}>
        <Tabs value={params.tab} indicatorColor="primary" textColor="primary" data-testid="ClusterTabList">
          {tabList.map((tab) => (
            <Tab
              key={tab.name}
              value={tab.name}
              label={t(`clusterDetail.${tab.name}`)}
              component={Link}
              to={generatePath(path, {
                accountId: params.accountId,
                projectId: params.projectId,
                clusterId: params.clusterId,
                tab: tab.name
              })}
              data-testid={tab.testId}
            />
          ))}
        </Tabs>
      </div>

      <Box mt={2}>
        <Switch>
          {tabList.map((tab) => (
            <Route key={tab.name} path={`${path}/${tab.name}/:subTab?`} component={tab.component} />
          ))}
        </Switch>
      </Box>
    </Box>
  );
};
