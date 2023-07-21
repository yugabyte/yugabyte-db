import { useRef } from 'react';
import type { Meta, StoryObj } from '@storybook/react';
import {
  YBAutoComplete,
  YBAutoCompleteProps
} from '../redesign/components/YBAutoComplete/YBAutoComplete';

const OPTIONS = [
  {
    label: 'Option 1',
    value: '1',
    group: 'Group 1'
  },
  {
    label: 'Option 2',
    value: '2',
    group: 'Group 1'
  },
  {
    label: 'Option 3',
    value: '3',
    group: 'Group 1'
  },
  {
    label: 'Option 4',
    value: '4',
    group: 'Group 2'
  },
  {
    label: 'Option 5',
    value: '5',
    group: 'Group 2'
  },
  {
    label: 'Option 6',
    value: '6',
    group: 'Group 2'
  },
  {
    label: 'Option 7',
    value: '7',
    group: 'Group 2'
  }
];

// More on how to set up stories at: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
const meta: Meta<YBAutoCompleteProps> = {
  title: 'Components/YBAutoComplete',
  component: YBAutoComplete,
  // This component will have an automatically generated Autodocs entry: https://storybook.js.org/docs/react/writing-docs/autodocs
  tags: ['autodocs'],
  parameters: {
    controls: {
      include: ['ybInputProps', 'options'],
      sort: 'alpha'
    }
  }
};

export default meta;
type Story = StoryObj<YBAutoCompleteProps>;

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
export const AutoComplete: Story = (args: any) => {
  const ref = useRef(null);
  // More on args: https://storybook.js.org/docs/react/writing-stories/args

  return (
    <YBAutoComplete
      options={(OPTIONS as unknown) as Record<string, string>[]}
      groupBy={(option: Record<string, any>) => option?.group} //group by
      getOptionLabel={(option: Record<string, any>) => option?.label}
      renderOption={(option: Record<string, any>) => option?.label}
      ref={ref.current}
      value={OPTIONS[0]}
      {...args}
    />
  );
};

export const AutoCompleteWithoutGrouping: Story = (args: any) => {
  const ref = useRef(null);
  // More on args: https://storybook.js.org/docs/react/writing-stories/args

  return (
    <YBAutoComplete
      options={(OPTIONS as unknown) as Record<string, string>[]}
      getOptionLabel={(option: Record<string, any>) => option?.label}
      renderOption={(option: Record<string, any>) => option?.label}
      ref={ref.current}
      value={OPTIONS[0]}
      {...args}
    />
  );
};

const args = {
  ybInputProps: {
    placeholder: 'Type here',
    error: false,
    helperText: 'Helper Text',
    disabled: false,
    fullWidth: true
  },
  options: OPTIONS
};

AutoComplete.args = args;
AutoCompleteWithoutGrouping.args = args;
