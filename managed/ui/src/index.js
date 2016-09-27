// Copyright (c) YugaByte, Inc.

import React from 'react';
import ReactDOM from 'react-dom';
import { Provider } from 'react-redux';
import { Router, browserHistory } from 'react-router';
import { IntlProvider } from 'react-intl';
import fetchRoutes from './routes';
import configureStore from './store/configureStore.js';
const store = configureStore();
import 'bootstrap/dist/css/bootstrap.css';
import '../src/stylesheets/custom.css';
ReactDOM.render(
  <Provider store={store}>
    <IntlProvider locale="en">
      <Router history={browserHistory}>
        {fetchRoutes(store)}
      </Router>
    </IntlProvider>
  </Provider>
  , document.getElementById('root'));
