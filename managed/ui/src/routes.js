// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Route, IndexRoute } from 'react-router';
import { validateToken, validateTokenSuccess, validateTokenFailure } from './actions/customers';
import App from './pages/App';
import Login from './pages/Login';
import Register from './pages/Register';
import AuthenticatedComponent from './pages/AuthenticatedComponent';
import Dashboard from './pages/Dashboard';
import UniverseDetail from './pages/UniverseDetail';
import Universes from './pages/Universes';
import Alerts from './pages/Alerts';
import ListUniverse from './pages/ListUniverse';
import SetupDataCenter from './pages/SetupDataCenter';
import Metrics from './pages/Metrics';
import TableDetail from './pages/TableDetail';

function validateSession(store, replacePath, callback) {
  let token = localStorage.getItem('customer_token');
  // If the token is null or invalid, we just re-direct to login page
  if(!token || token === '') {
    replacePath('/login');
  } else {
    store.dispatch(validateToken(token))
      .then((response) => {
        if (!response.error) {
          store.dispatch(validateTokenSuccess(response.payload));
        } else {
          localStorage.clear();
          replacePath('/login');
          callback();
          store.dispatch(validateTokenFailure(response.payload));
        }
      });
  }
  callback();
}

export default (store) => {
  const authenticatedSession = (nextState, replace, callback) => {
    validateSession(store, replace, callback);
  };

  const checkIfAuthenticated = (prevState, nextState, replace, callback) => {
    validateSession(store, replace, callback);
  }

  return (
    // We will have two different routes, on which is authenticated route
    // rest un-authenticated route
    <Route path="/" component={App}>
      <Route path="/login" component={Login} />
      <Route path="/register" component={Register} />
      <Route onEnter={authenticatedSession} onChange={checkIfAuthenticated} component={AuthenticatedComponent}>
        <IndexRoute component={Dashboard} />
        <Route path="/universes" component={Universes} >
          <IndexRoute component={ListUniverse} />
          <Route path="/universes/:uuid" component={UniverseDetail} />
          <Route path="/universes/:uuid/tables/:tableUUID" component={TableDetail}/>
        </Route>
        <Route path="/tasks" component={Alerts} />
        <Route path="/metrics" component={Metrics} />
        <Route path="/setup_datacenter" component={SetupDataCenter} />
      </Route>
    </Route>
  );
};
