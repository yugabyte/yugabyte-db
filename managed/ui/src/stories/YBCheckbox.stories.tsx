import type { Meta, StoryObj } from '@storybook/react';
import { YBCheckbox, YBCheckboxProps } from '../redesign/components/YBCheckbox/YBCheckbox';

// More on how to set up stories at: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
const meta: Meta<YBCheckboxProps> = {
  title: 'Components/YBCheckbox',
  component: YBCheckbox,
  // This component will have an automatically generated Autodocs entry: https://storybook.js.org/docs/react/writing-docs/autodocs
  tags: ['autodocs'],
  parameters: {
    controls: {
      exclude: [
        'action',
        'inputProps',
        'focusVisibleClassName',
        'onFocusVisible',
        'ref',
        'TouchRippleProps'
      ],
      sort: 'alpha'
    }
  },
  argTypes: {
    label: {
      control: { type: 'text' }
    }
  }
};

export default meta;
type Story = StoryObj<YBCheckboxProps>;

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
export const Checkbox: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args
  args: {
    disabled: false,
    centerRipple: false,
    label: 'Test',
    disableFocusRipple: false,
    disableTouchRipple: false,
    indeterminate: false
  }
};
