// Copyright (c) YugaByte, Inc.

import React from 'react';
import ReactDOM from 'react-dom';
import { Provider } from 'react-redux';
import { Router, browserHistory } from 'react-router';
import fetchRoutes from './routes';
import configureStore from './store/configureStore.js';
const store = configureStore();
import 'bootstrap/dist/css/bootstrap.css';
import $ from 'jquery';
window.$ = $;

ReactDOM.render(
  <Provider store={store}>
    <Router history={browserHistory}>
      {fetchRoutes(store)}
    </Router>
  </Provider>
  , document.getElementById('root'));
