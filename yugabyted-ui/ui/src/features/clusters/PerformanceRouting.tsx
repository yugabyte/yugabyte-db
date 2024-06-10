import React, { FC } from 'react';
import { Switch, Route, useRouteMatch } from 'react-router-dom';
import { PerformanceDetails } from '@app/features/clusters/details/performance/PerformanceDetails';

export const PerformanceRouting: FC = () => {
  const { path } = useRouteMatch<App.RouteParams>();

  return (
    <Switch>
      <Route path={`${path}/:tab?`}>
        <PerformanceDetails />
      </Route>
    </Switch>
  );
};
