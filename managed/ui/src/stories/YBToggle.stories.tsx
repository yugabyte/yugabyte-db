import type { Meta, StoryObj } from '@storybook/react';
import { YBToggle, YBToggleProps } from '../redesign/components/YBToggle/YBToggle';

// More on how to set up stories at: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
const meta: Meta<YBToggleProps> = {
  title: 'Components/YBToggle',
  component: YBToggle,
  // This component will have an automatically generated Autodocs entry: https://storybook.js.org/docs/react/writing-docs/autodocs
  tags: ['autodocs'],
  parameters: {
    controls: {
      exclude: [
        'action',
        'focusVisibleClassName',
        'FormControlProps',
        'inputProps',
        'onFocusVisible',
        'ref',
        'TouchRippleProps'
      ],
      sort: 'alpha'
    }
  }
};

export default meta;
type Story = StoryObj<YBToggleProps>;

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
export const Toggle: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args
  args: {
    disabled: false,
    centerRipple: false,
    label: 'Test',
    disableFocusRipple: false,
    disableTouchRipple: false
  }
};
