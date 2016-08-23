// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Route, IndexRoute } from 'react-router';
import { validateToken, validateTokenSuccess, validateTokenFailure } from './actions/customers';

import App from './pages/App';
import Login from './pages/Login';
import Register from './pages/Register';
import DashboardWrapper from './pages/DashboardWrapper';
import Dashboard from './pages/Dashboard';
import UniverseDetail from './pages/UniverseDetail';

export default (store) => {
  const authenticatedSession = (nextState, replace, callback) => {
    function validateSession() {
      let token = localStorage.getItem('customer_token');
      // If the token is null or invalid, we just re-direct to login page
      if(!token || token === '') {
        replace('/login');
      } else {
        store.dispatch(validateToken(token))
        .then((response) => {
          if (!response.error) {
            store.dispatch(validateTokenSuccess(response.payload));
          } else {
            localStorage.removeItem('customer_token');
            store.dispatch(validateTokenFailure(response.payload));
            replace('/login');
          }
        });
      }
      callback();
    }
    validateSession();
  };

  return (
    // We will have two different routes, on which is authenticated route
    // rest un-authenticated route
    <Route path="/" component={App}>
      <Route path="/login" component={Login} />
      <Route path="/register" component={Register} />
      <Route onEnter={authenticatedSession} component={DashboardWrapper}>
        <IndexRoute component={Dashboard} />
        <Route path="/home" component={Dashboard} />
        <Route path="/universes/:uuid" component={UniverseDetail} />
      </Route>
    </Route>
  );
};
