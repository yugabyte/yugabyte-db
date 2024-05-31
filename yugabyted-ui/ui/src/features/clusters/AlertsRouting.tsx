import React, { FC } from 'react';
import { Switch, Route, useRouteMatch } from 'react-router-dom';
import { AlertTabs } from './details/alerts/AlertTabs';

export const AlertsRouting: FC = () => {
  const { path } = useRouteMatch<App.RouteParams>();

  return (
    <Switch>
      <Route path={`${path}/:tab?`}>
        <AlertTabs />
      </Route>
    </Switch>
  );
};
