import type { Preview } from '@storybook/react-vite';
import { CssBaseline, ThemeProvider } from '@material-ui/core';
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import { useMemo, type ReactNode } from 'react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { YBCssBaseline, YBThemeProvider, yba } from '@yugabyte-ui-library/core';
import { initialize, mswLoader } from 'msw-storybook-addon';
import { http, HttpResponse } from 'msw';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';

import { mainTheme } from '../src/redesign/theme/mainTheme';
import en from '../src/translations/en.json';

void i18n.use(initReactI18next).init({
  resources: { en },
  fallbackLng: 'en',
  interpolation: { escapeValue: false }
});

const createStorybookQueryClient = () => {
  return new QueryClient({
    defaultOptions: {
      queries: {
        retry: false,
        refetchOnWindowFocus: false
      }
    }
  });
};

const StorybookQueryClientProvider = ({
  storyId,
  children
}: {
  storyId: string;
  children: ReactNode;
}) => {
  const queryClient = useMemo(() => createStorybookQueryClient(), [storyId]);
  return <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>;
};

const { ybaTheme } = yba;

const apiBase = import.meta.env.VITE_YUGAWARE_API_URL ?? 'http://localhost:9000/api/v2';
const API_ORIGIN = apiBase.startsWith('http') ? new URL(apiBase).origin : window.location.origin;

export const decorators = [
  (Story: React.ComponentType, context: { id: string }) => (
    <StorybookQueryClientProvider storyId={context.id}>
      <YBThemeProvider theme={ybaTheme}>
        <ThemeProvider theme={mainTheme}>
          <CssBaseline />
          <YBCssBaseline />
          <Story />
        </ThemeProvider>
      </YBThemeProvider>
    </StorybookQueryClientProvider>
  )
];

initialize({
  onUnhandledRequest({ url }, print) {
    try {
      const requestOrigin = new URL(url).origin;
      if (requestOrigin === API_ORIGIN) {
        print.warning();
      }
    } catch {
      print.warning();
    }
  }
});

const preview: Preview = {
  parameters: {
    actions: { argTypesRegex: '^on[A-Z].*' },
    controls: {
      matchers: {
        color: /(background|color)$/i,
        date: /Date$/
      }
    }
  },
  loaders: [mswLoader]
};

export default preview;
