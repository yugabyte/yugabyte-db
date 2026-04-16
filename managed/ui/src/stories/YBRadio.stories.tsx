import type { Meta, StoryObj } from '@storybook/react';
import { YBRadioGroup, YBRadioGroupProps } from '../redesign/components/YBRadio/YBRadio';

// More on how to set up stories at: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
const meta: Meta<YBRadioGroupProps> = {
  title: 'Components/YBRadio',
  component: YBRadioGroup,
  // This component will have an automatically generated Autodocs entry: https://storybook.js.org/docs/react/writing-docs/autodocs
  tags: ['autodocs'],
  parameters: {
    controls: {
      exclude: ['label', 'ref'],
      sort: 'alpha'
    }
  }
};

export default meta;
type Story = StoryObj<YBRadioGroupProps>;

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
export const RadioGroup: Story = {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args
  args: {
    options: [
      { value: 'a', label: 'Option A' },
      { value: 'b', label: 'Option B', disabled: true },
      { value: 'c', label: 'Option C' }
    ]
  }
};
