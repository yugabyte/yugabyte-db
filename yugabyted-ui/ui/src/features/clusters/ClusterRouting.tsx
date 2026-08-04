import React, { FC } from 'react';
import { Switch, Route, useRouteMatch } from 'react-router-dom';
import { OverviewDetails } from '@app/features/clusters/details/overview/OverviewDetails';
import { QueryParamProvider } from 'use-query-params';

export const ClusterRouting: FC = () => {
  const { path } = useRouteMatch<App.RouteParams>();

  return (
    <QueryParamProvider ReactRouterRoute={Route}>
      <Switch>
        <Route path={`${path}/:tab?`}>
          <OverviewDetails />
        </Route>
      </Switch>
    </QueryParamProvider>
  );
};
