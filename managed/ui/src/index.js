// Copyright (c) YugaByte, Inc.

import React from 'react';
import ReactDOM from 'react-dom';
import { Provider } from 'react-redux';
import { Router, browserHistory } from 'react-router';
import { IntlProvider } from 'react-intl';
import { QueryClient, QueryClientProvider } from 'react-query';
import fetchRoutes from './routes';
import configureStore from './store/configureStore.js';
import 'intl';
import 'intl/locale-data/jsonp/en.js';

const store = configureStore();
const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: false, // by default it would retry any failed query for 3 times
      refetchOnWindowFocus: false // no need to refetch all queries when a user returns to the app
    }
  }
});

const AppWrapper = () => (
  <Provider store={store}>
    <QueryClientProvider client={queryClient}>
      <IntlProvider locale="en">
        <Router history={browserHistory}>
          {fetchRoutes(store)}
        </Router>
      </IntlProvider>
    </QueryClientProvider>
  </Provider>
);

ReactDOM.render(<AppWrapper />, document.getElementById('root'));
