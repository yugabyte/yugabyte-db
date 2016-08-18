// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Route, IndexRoute } from 'react-router';

import App from './pages/App';
import Login from './pages/Login';
import Register from './pages/Register';
import Dashboard from './pages/Dashboard';

export default (
  <Route path="/" component={App}>
    <IndexRoute component={Dashboard} />
      <Route path="/login" component={Login} />
      <Route path="/register" component={Register} />
      <Route path="/home" component={Dashboard} />
  </Route>
);
