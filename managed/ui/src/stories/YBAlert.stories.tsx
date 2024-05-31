import { Box, Typography } from '@material-ui/core';
import type { Meta, StoryObj } from '@storybook/react';
import { YBAlert, AlertProps, AlertVariant } from '../redesign/components/YBAlert/YBAlert';

// More on how to set up stories at: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
const meta: Meta<AlertProps> = {
  title: 'Components/YBAlert',
  component: YBAlert,
  // This component will have an automatically generated Autodocs entry: https://storybook.js.org/docs/react/writing-docs/autodocs
  tags: ['autodocs'],
  parameters: {
    controls: {
      include: ['variant'],
      sort: 'alpha'
    }
  },
  args: {}
};

export default meta;
type Story = StoryObj<AlertProps>;

const ALertContent = () => (
  <Box my={1}>
    <Box>
      <Typography variant="body1">Lorem Ipsum dolor sit amet.</Typography>
    </Box>
    <Box>
      {[
        'Consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.',
        'Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.',
        'Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur.'
      ].map((error, index) => (
        <Box mt={0.5}>
          <Typography variant="body2">
            {index + 1}. {error}.
          </Typography>
        </Box>
      ))}
    </Box>
  </Box>
);

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
export const InteractiveAlert: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args{
  args: {
    open: true,
    variant: AlertVariant.Error,
    text: ALertContent()
  }
};

export const Info: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args{
  args: {
    open: true,
    variant: AlertVariant.Info,
    text: ALertContent()
  },
  parameters: {
    controls: {
      exclude: ['variant']
    }
  }
};
export const Warning: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args{
  args: {
    open: true,
    variant: AlertVariant.Warning,
    text: ALertContent()
  },
  parameters: {
    controls: {
      exclude: ['variant']
    }
  }
};
export const Error: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args{
  args: {
    open: true,
    variant: AlertVariant.Error,
    text: ALertContent()
  },
  parameters: {
    controls: {
      exclude: ['variant']
    }
  }
};
export const Success: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args{
  args: {
    open: true,
    variant: AlertVariant.Success,
    text: ALertContent()
  },
  parameters: {
    controls: {
      exclude: ['variant']
    }
  }
};
export const InProgress: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args{
  args: {
    open: true,
    variant: AlertVariant.InProgress,
    text: ALertContent()
  },
  parameters: {
    controls: {
      exclude: ['variant']
    }
  }
};
