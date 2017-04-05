// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Route, IndexRoute, browserHistory } from 'react-router';
import { validateToken, validateTokenSuccess,
  validateTokenFailure, fetchCustomerCount } from './actions/customers';
import App from './app/App';
import Login from './pages/Login';
import Register from './pages/Register';
import AuthenticatedComponent from './pages/AuthenticatedComponent';
import Dashboard from './pages/Dashboard';
import UniverseDetail from './pages/UniverseDetail';
import Universes from './pages/Universes';
import Tasks from './pages/Tasks';
import Alerts from './pages/Alerts';
import ListUniverse from './pages/ListUniverse';
import Metrics from './pages/Metrics';
import DataCenterConfiguration from './pages/DataCenterConfiguration';
import TableDetail from './pages/TableDetail';
import Help from './pages/Help';
import Profile from './pages/Profile';

function validateSession(store, replacePath, callback) {
  let token = localStorage.getItem('customer_token');
  // If the token is null or invalid, we just re-direct to login page
  if(!token || token === '') {
    store.dispatch(fetchCustomerCount()).then((response) => {
      if (!response.error) {
        var responseData = response.payload.data;
        if (responseData && responseData.count === 0) {
          browserHistory.push('/register');
        }
      }
    })
    browserHistory.push('/login');
  } else {
    store.dispatch(validateToken(token))
      .then((response) => {
        if (!response.error) {
          store.dispatch(validateTokenSuccess(response.payload));
        } else {
          localStorage.clear();
          browserHistory.push('/login');
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
        <Route path="/tasks" component={Tasks} />
        <Route path="/metrics" component={Metrics} />
        <Route path="/config" component={DataCenterConfiguration} />
        <Route path="/alerts" component={Alerts}/>
        <Route path="/help" component={Help}/>
        <Route path="/profile" component={Profile}/>
      </Route>
    </Route>
  );
};
