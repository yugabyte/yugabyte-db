import React, { FC } from 'react';
import { Switch, Route, useRouteMatch } from 'react-router-dom';
// import { ClustersList } from '@app/features/clusters/list/ClustersList';
import { ClusterDetails } from '@app/features/clusters/details/ClusterDetails';
// import { WizardLoader } from '@app/features/clusters/wizard/Wizard';
// import { QueryParamProvider } from 'use-query-params';

export const ClustersRouting: FC = () => {
  const { path } = useRouteMatch<App.RouteParams>();

  return (
    <Switch>
      {/* <Route path={`${path}/new`} exact>
        <QueryParamProvider ReactRouterRoute={Route}>
          <WizardLoader />
        </QueryParamProvider>
      </Route> */}
      <Route path={`${path}/:tab?`}>
        <ClusterDetails />
      </Route>
      {/* <Route>
        <ClustersList />
      </Route> */}
    </Switch>
  );
};
