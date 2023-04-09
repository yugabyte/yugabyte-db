import React, { FC } from 'react';
import { BrowserRouter, Switch, Route } from 'react-router-dom';
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import { QueryClient, QueryClientProvider } from 'react-query';
import { CssBaseline, ThemeProvider } from '@material-ui/core';
import en from '@app/translations/en.json';
import { mainTheme } from '@app/theme/mainTheme';
import { MainLayout } from '@app/features';

const CACHE_TIMEOUT = 15 * 60 * 1000; // keep data in react-query cache for 15 mins
const IS_DEV_MODE = import.meta.env.NODE_ENV === 'development';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: false,
      staleTime: CACHE_TIMEOUT,
      refetchOnMount: false, // don't refetch on mount when result is already in cache (i.e. query pre-fetched earlier)
      refetchOnWindowFocus: false
    }
  }
});

void i18n.use(initReactI18next).init({
  resources: { en },
  lng: 'en',
  debug: IS_DEV_MODE,
  compatibilityJSON: 'v3', // Need this for plurals to work using older json format
  interpolation: {
    escapeValue: false
  }
});

export const App: FC = () => {
  // TODO: wrap with <React.StrictMode> once moved to MUI 5
  return (
    <QueryClientProvider client={queryClient}>
      <ThemeProvider theme={mainTheme}>
        {/* <ToastProvider> */}
          <BrowserRouter>
            <CssBaseline />
            <Switch>              
              {/* <Route component={ClusterDetails} /> */}
              <Route component={MainLayout} />
            </Switch>
          </BrowserRouter>
        {/* </ToastProvider> */}
      </ThemeProvider>
    </QueryClientProvider>
  );
};
