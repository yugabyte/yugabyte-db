import React, { FC } from 'react';
import { Switch, Route, useRouteMatch } from 'react-router-dom';
import { OverviewDetails } from '@app/features/clusters/details/overview/OverviewDetails';

export const OverviewRouting: FC = () => {
  const { path } = useRouteMatch<App.RouteParams>();

  return (
    <Switch>
      <Route path={`${path}/:tab?`}>
        <OverviewDetails />
      </Route>
    </Switch>
  );
};
