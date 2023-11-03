import React, { FC, ComponentType } from 'react';
import { useTranslation } from 'react-i18next';
import { generatePath, Link, Route, Switch, useRouteMatch } from 'react-router-dom';
import {
  Tabs,
  Tab,
  makeStyles,
  Box,
} from '@material-ui/core';

import { QueryParamProvider } from 'use-query-params';
import { LiveQueries } from './LiveQueries';
import { SlowQueries } from './SlowQueries';
import { Metrics } from '@app/features/clusters/details/performance/metrics';

interface ContextInput {
  state: Record<string, unknown>;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  dispatch: (action: { type: string; payload?: any }) => void;
}

export const ClusterContext = React.createContext<ContextInput | null>(null);

const useStyles = makeStyles((theme) => ({
  tabSectionContainer: {
    display: 'flex',
    alignItems: 'center',
    width: '100%',
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`
  },
  loadingSection: {
    padding: theme.spacing(1.5),
    minHeight: 64,
    border: `1px solid ${theme.palette.grey[200]}`
  },
  loadingHelpDocs: {
    display: 'flex',
    justifyContent: 'flex-start',
    padding: theme.spacing(1.5, 2),
    borderColor: theme.palette.grey[200],
    alignItems: 'center',

    '& > *': {
      marginRight: theme.spacing(3)
    }
  },
  clusterInfo: {
    display: 'flex',
    padding: theme.spacing(2)
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: 500,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase'
  },
  link: {
    textDecoration: 'none',
    color: theme.palette.text.primary,
    fontWeight: 400,
    '&:hover': {
      textDecoration: 'none'
    }
  },
  region: {
    display: 'flex',
    alignItems: 'center',
    '& > svg': {
      marginRight: theme.spacing(1)
    }
  },
  docsIcon: {
    width: theme.spacing(2),
    height: theme.spacing(2),
    color: theme.palette.grey[600],
    marginRight: theme.spacing(1.5)
  },
  dangerColor: {
    color: theme.palette.error.main
  },
  docLinks: {
    display: 'flex',
    alignItems: 'center'
  },
  resumeBtn: {
    marginLeft: theme.spacing(1.25)
  }
}));

export interface ITabListItem {
  name: string;
  component: ComponentType;
  testId: string;
}

const tabList: ITabListItem[] = [
  {
    name: 'metrics',
    component: Metrics,
    testId: 'ClusterTabList-Performance'
  },
  {
    name: 'live_queries',
    component: LiveQueries,
    testId: 'ClusterTabList-Performance'
  },
  {
    name: 'slow_queries',
    component: SlowQueries,
    testId: 'ClusterTabList-Performance'
  },
];

export const PerformanceDetails: FC = () => {
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
              label={t((`clusterDetail.performance.tabs.${tab.name}` as unknown) as TemplateStringsArray)}
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
        <QueryParamProvider ReactRouterRoute={Route}>
            <Switch>
              {tabList.map((tab) => (
                <Route key={tab.name} path={`${path}/${tab.name}`} component={tab.component} />
              ))}
            </Switch>
        </QueryParamProvider>
      </Box>
    </Box>
  );
};
