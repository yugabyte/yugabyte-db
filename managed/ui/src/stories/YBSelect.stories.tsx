import { MenuItem } from '@material-ui/core';
import type { Meta, StoryObj } from '@storybook/react';
import { YBSelect, YBSelectProps } from '../redesign/components/YBSelect/YBSelect';

// More on how to set up stories at: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
const meta: Meta<YBSelectProps> = {
  title: 'Components/YBSelect',
  component: YBSelect,
  // This component will have an automatically generated Autodocs entry: https://storybook.js.org/docs/react/writing-docs/autodocs
  tags: ['autodocs'],
  parameters: {
    controls: {
      include: ['value', 'label', 'error', 'disabled', 'helperText', 'fullWidth'],
      sort: 'alpha'
    }
  },
  args: {}
};

export default meta;
type Story = StoryObj<YBSelectProps>;

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
export const List: Story = (args: any) => {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args

  return (
    <YBSelect {...args}>
      <MenuItem value="111" style={{ display: 'none' }}>
        Select smth
      </MenuItem>
      <MenuItem value="111">Option 1</MenuItem>
      <MenuItem value="222">Option 2</MenuItem>
      <MenuItem value="333">Option 3</MenuItem>
    </YBSelect>
  );
};

List.args = {
  error: false,
  fullWidth: false,
  disabled: false,
  label: '',
  helperText: 'Helper text',
  defaultValue: '111'
};
