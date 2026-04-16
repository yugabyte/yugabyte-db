import type { Meta } from '@storybook/react';
import { YBButton, YBButtonProps } from '../redesign/components/YBButton/YBButton';
import { Settings } from '@material-ui/icons';

// More on how to set up stories at: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
const meta: Meta<YBButtonProps> = {
  title: 'Components/YBButton',
  component: YBButton,
  // This component will have an automatically generated Autodocs entry: https://storybook.js.org/docs/react/writing-docs/autodocs
  tags: ['autodocs'],
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
  },
  argTypes: {
    size: {
      options: ['small', 'medium', 'large']
    },
    variant: {
      options: ['primary', 'secondary', 'ghost', 'gradient', 'pill']
    }
  },
  args: {
    disabled: false
  }
};
export default meta;

export const InteractiveButton = ({ startIcon, endIcon, ...args }: any) => (
  <div>
    <YBButton {...args}>Default</YBButton>
    <p />
    <YBButton {...args} startIcon={<Settings />}>
      Start Icon
    </YBButton>
    <p />
    <YBButton {...args} endIcon={<Settings />}>
      End Icon
    </YBButton>
    <p />
    <YBButton {...args} startIcon={<Settings />} endIcon={<Settings />}>
      Both Icons
    </YBButton>
    <p />
    <YBButton {...args} size="small">
      Small size
    </YBButton>
    <p />
    <YBButton {...args} disabled>
      Disabled
    </YBButton>
    <p />
    <YBButton {...args} disabled showSpinner>
      Loading
    </YBButton>
  </div>
);
InteractiveButton.args = {
  variant: 'primary',
  size: 'medium'
};

export const Primary = () => (
  <div>
    <YBButton variant="primary">Default</YBButton>
    <p />
    <YBButton variant="primary" startIcon={<Settings />}>
      Start Icon
    </YBButton>
    <p />
    <YBButton variant="primary" endIcon={<Settings />}>
      End Icon
    </YBButton>
    <p />
    <YBButton variant="primary" startIcon={<Settings />} endIcon={<Settings />}>
      Both Icons
    </YBButton>
    <p />
    <YBButton variant="primary" size="small">
      Small size
    </YBButton>
    <p />
    <YBButton variant="primary" disabled>
      Disabled
    </YBButton>
    <p />
    <YBButton variant="primary" disabled showSpinner>
      Loading
    </YBButton>
  </div>
);

export const Secondary = () => (
  <div>
    <YBButton variant="secondary">Default</YBButton>
    <p />
    <YBButton variant="secondary" startIcon={<Settings />}>
      Start Icon
    </YBButton>
    <p />
    <YBButton variant="secondary" endIcon={<Settings />}>
      End Icon
    </YBButton>
    <p />
    <YBButton variant="secondary" startIcon={<Settings />} endIcon={<Settings />}>
      Both Icons
    </YBButton>
    <p />
    <YBButton variant="secondary" size="small">
      Small size
    </YBButton>
    <p />
    <YBButton variant="secondary" disabled>
      Disabled
    </YBButton>
    <p />
    <YBButton variant="secondary" disabled showSpinner>
      Loading
    </YBButton>
  </div>
);

export const Ghost = () => (
  <div>
    <YBButton variant="ghost">Default</YBButton>
    <p />
    <YBButton variant="ghost" startIcon={<Settings />}>
      Start Icon
    </YBButton>
    <p />
    <YBButton variant="ghost" endIcon={<Settings />}>
      End Icon
    </YBButton>
    <p />
    <YBButton variant="ghost" startIcon={<Settings />} endIcon={<Settings />}>
      Both Icons
    </YBButton>
    <p />
    <YBButton variant="ghost" size="small">
      Small size
    </YBButton>
    <p />
    <YBButton variant="ghost" disabled>
      Disabled
    </YBButton>
    <p />
    <YBButton variant="ghost" disabled showSpinner>
      Loading
    </YBButton>
  </div>
);
