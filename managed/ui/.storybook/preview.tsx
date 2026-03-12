import type { Preview } from '@storybook/react-vite';
import { ThemeProvider } from '@material-ui/core';
import { mainTheme } from '../src/redesign/theme/mainTheme';

const preview: Preview = {
  parameters: {
    actions: { argTypesRegex: '^on[A-Z].*' },
    controls: {
      matchers: {
        color: /(background|color)$/i,
        date: /Date$/
      }
    }
  }
};

export const decorators = [
  (Story: any) => (
    <ThemeProvider theme={mainTheme}>
      <Story />
    </ThemeProvider>
  )
];

export default preview;
