import type { Meta } from '@storybook/react';
import { useState } from 'react';
import { Box } from '@material-ui/core';
import {
  YBButtonGroup,
  YBButtonGroupProps
} from '../redesign/components/YBButtonGroup/YBButtonGroup';

const meta: Meta<YBButtonGroupProps<Number>> = {
  title: 'Components/YBButtonGroup',
  component: YBButtonGroup,
  tags: ['autodocs'],
  args: {
    disabled: false,
    values: [1, 2, 3, 4, 5],
    variant: 'contained',
    color: 'default'
  },
  parameters: {
    controls: {
      exclude: [
        'action',
        'focusVisibleClassName',
        'onFocusVisible',
        'ref',
        'tabIndex',
        'TouchRippleProps',
        'children',
        'startIcon',
        'endIcon'
      ],
      sort: 'alpha'
    }
  }
};

export default meta;

export const ButtonGroup = (args: any) => {
  const [selectedVal, setSelectedVal] = useState<number>(args.values[0]);
  const handleSelect = (val: number) => {
    setSelectedVal(val);
  };

  return (
    <Box mb={2.5} mt={0.5}>
      <YBButtonGroup {...args} selectedNum={selectedVal} handleSelect={handleSelect} />
    </Box>
  );
};
