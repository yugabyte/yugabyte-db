import React, { FC } from 'react';
import { Switch, Route, useRouteMatch } from 'react-router-dom';
import { DatabasesDetails } from '@app/features/clusters/details/tables/DatabasesDetails';

export const DatabasesRouting: FC = () => {
  const { path } = useRouteMatch<App.RouteParams>();

  return (
    <Switch>
      <Route path={`${path}/:tab?`}>
        <DatabasesDetails />
      </Route>
    </Switch>
  );
};
