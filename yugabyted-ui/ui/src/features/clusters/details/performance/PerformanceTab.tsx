import { Box, makeStyles, Tab, Tabs } from '@material-ui/core';
import React, { ComponentType, FC } from 'react';
import { useTranslation } from 'react-i18next';
import { generatePath, Redirect, Route, Switch, useRouteMatch } from 'react-router';
import { Link } from 'react-router-dom';
import { LiveQueries } from './LiveQueries';
import { SlowQueries } from './SlowQueries';
import { QueryParamProvider } from 'use-query-params';
import { Metrics } from '@app/features/clusters/details/performance/metrics';

const useStyles = makeStyles((theme) => ({
  tabsContainer: {
    marginBottom: theme.spacing(1),
    '& .MuiTabs-indicator': {
      display: 'none'
    },
    '& .MuiTabs-fixed': {
      boxShadow: 'none'
    }
  },
  tabButton: {
    textAlign: 'left',
    marginRight: theme.spacing(1),
    minHeight: theme.spacing(4),
    maxHeight: theme.spacing(4),
    marginBottom: theme.spacing(0.5),
    padding: theme.spacing(0, 2),
    boxSizing: 'border-box',

    '&:hover': {
      boxShadow: 'none',
      borderRadius: theme.shape.borderRadius
    },
    '&.Mui-selected': {
      background: theme.palette.grey[300],
      borderRadius: theme.spacing(1)
    },
    '& .MuiTab-wrapper': {
      alignItems: 'flex-start'
    }
  }
}));

const tabComponent: Record<string, ComponentType> = {
  metrics: Metrics,
  live_queries: LiveQueries,
  slow_queries: SlowQueries
};

const performanceTabs = Object.keys(tabComponent);

export const PerformanceTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { path, url, params } = useRouteMatch<App.RouteParams>();

  if (!params.subTab) {
    return <Redirect to={`${url}/${performanceTabs[0]}`} />;
  }

  return (
    <>
      <Box display="flex" alignItems="center" className={classes.tabsContainer}>
        <Tabs variant="fullWidth" value={params?.subTab.toString()} indicatorColor="primary" textColor="primary">
          {performanceTabs.map((performanceTab) => (
            <Tab
              key={performanceTab}
              value={performanceTab}
              className={classes.tabButton}
              label={t((`clusterDetail.performance.tabs.${performanceTab}` as unknown) as TemplateStringsArray)}
              component={Link}
              to={generatePath(path, { ...params, subTab: performanceTab })}
            />
          ))}
        </Tabs>
      </Box>
      <div>
        <QueryParamProvider ReactRouterRoute={Route}>
          <Switch>
            {performanceTabs.map((tab) => (
              <Route key={tab} path={`${path}/${tab}`} component={tabComponent[tab]} />
            ))}
          </Switch>
        </QueryParamProvider>
      </div>
    </>
  );
};
