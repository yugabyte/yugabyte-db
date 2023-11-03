import type { Meta, StoryObj } from '@storybook/react';
import { YBInput, YBInputProps } from '../redesign/components/YBInput/YBInput';

// More on how to set up stories at: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
const meta: Meta<YBInputProps> = {
  title: 'Components/YBTextbox',
  component: YBInput,
  // This component will have an automatically generated Autodocs entry: https://storybook.js.org/docs/react/writing-docs/autodocs
  tags: ['autodocs'],
  parameters: {
    controls: {
      include: ['value', 'placeholder', 'label', 'error', 'disabled', 'helperText', 'fullWidth'],
      sort: 'alpha'
    }
  }
};

export default meta;
type Story = StoryObj<YBInputProps>;

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
export const Textbox: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args
  args: {
    error: false,
    fullWidth: false,
    disabled: false,
    label: '',
    placeholder: 'Placeholder',
    helperText: 'Helper text'
  }
};
