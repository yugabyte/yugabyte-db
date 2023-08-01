import React, { FC } from 'react';
import { Switch, Route, useRouteMatch } from 'react-router-dom';
import { MigrationTabs } from './details/migration/MigrationTabs';

export const MigrationRouting: FC = () => {
  const { path } = useRouteMatch<App.RouteParams>();

  return (
    <Switch>
      <Route path={`${path}/:tab?`}>
        <MigrationTabs />
      </Route>
    </Switch>
  );
};
