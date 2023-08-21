import React, { FC } from 'react';
import { Switch, Route, useRouteMatch } from 'react-router-dom';
import { MigrationOverview } from './details/migration/MigrationOverview';
import { Box } from '@material-ui/core';

export const MigrationRouting: FC = () => {
  const { path } = useRouteMatch<App.RouteParams>();

  return (
    <Switch>
      <Route path={`${path}/:tab?`}>
        {/* <MigrationTabs /> */}
        <Box pt={2}>
          <MigrationOverview />
        </Box>
      </Route>
    </Switch>
  );
};
