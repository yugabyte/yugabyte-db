// Copyright (c) YugabyteDB, Inc.
// import '@yugabyte-ui-library/core/dist/esm/YBClassnameSetup';
import React from 'react';
import i18n from 'i18next';
import ReactDOM from 'react-dom';
import { CssBaseline, ThemeProvider } from '@material-ui/core';
import { mainTheme } from './redesign/theme/mainTheme';
import { initReactI18next } from 'react-i18next';
import { Provider } from 'react-redux';
import { Router, browserHistory } from 'react-router';
import { QueryClient, QueryClientProvider } from 'react-query';
import fetchRoutes from './routes';
import configureStore from './store/configureStore.js';
import en from './translations/en.json';
// import '@yugabytedb/ui-components';
import 'intl';
import 'intl/locale-data/jsonp/en.js';
import { YBCssBaseline, YBThemeProvider, yba } from '@yugabyte-ui-library/core';
import Leaflet from 'leaflet';

window.L = Leaflet.noConflict();

const { ybaTheme } = yba;

const store = configureStore();
const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: false, // by default it would retry any failed query for 3 times
      refetchOnWindowFocus: false // no need to refetch all queries when a user returns to the app
    }
  }
});

void i18n.use(initReactI18next).init({
  resources: { en },
  fallbackLng: 'en',
  interpolation: {
    escapeValue: false
  }
});

const AppWrapper = () => (
  <Provider store={store}>
    <QueryClientProvider client={queryClient}>
      <YBThemeProvider theme={ybaTheme}>
        <ThemeProvider theme={mainTheme}>
          <Router history={browserHistory}>
            <CssBaseline />
            <YBCssBaseline />
            {fetchRoutes(store)}
          </Router>
        </ThemeProvider>
      </YBThemeProvider>
    </QueryClientProvider>
  </Provider>
);

ReactDOM.render(<AppWrapper />, document.getElementById('root'));
