import { useState } from 'react';
import { Tab as MUItab, Tabs as MUITabs, Box, Typography } from '@material-ui/core';
import type { Meta } from '@storybook/react';

// More on how to set up stories at: https://storybook.js.org/docs/react/writing-stories/introduction#default-export
const meta: Meta = {
  title: 'Components/YBTabs',
  //   component: YBSelect,
  // This component will have an automatically generated Autodocs entry: https://storybook.js.org/docs/react/writing-docs/autodocs
  tags: ['autodocs'],
  args: { disabled: false }
};

export default meta;

// More on component templates: https://storybook.js.org/docs/react/writing-stories/introduction#using-args
export const Tabs = (args: any) => {
  // More on args: https://storybook.js.org/docs/react/writing-stories/args
  const [currentTab, setTab] = useState<string>('tab1');

  const handleChange = (_: any, tab: string) => {
    setTab(tab);
  };

  return (
    <Box mb={2.5} mt={0.5}>
      <MUITabs
        value={currentTab}
        indicatorColor="primary"
        onChange={handleChange}
        aria-label="tab section example"
        {...args}
      >
        <MUItab label="YugabyteDB Open Source" value="tab1" disabled={args.disabled} />
        <MUItab label="Yugabyte Platform" value="tab2" disabled={args.disabled} />
        <MUItab label="YugabyteDB Managed" value="tab3" disabled={args.disabled} />
      </MUITabs>
      <Box>
        {currentTab === 'tab1' && (
          <Box m={2}>
            <Typography variant="h2">{'YugabyteDB Open Source'}</Typography>
            <Box mt={1}>
              <Typography variant="body2">
                {
                  'YugabyteDB is the open source, high-performance, distributed SQL database for global, internet-scale apps.'
                }
              </Typography>
            </Box>
          </Box>
        )}
        {currentTab === 'tab2' && (
          <Box m={2}>
            <Typography variant="h2">{'Yugabyte Platform'}</Typography>
            <Box mt={1}>
              <Typography variant="body2">
                {
                  'Deploy, manage, and monitor YugabyteDB across the planet in any and every cloud from a single console.'
                }
              </Typography>
            </Box>
          </Box>
        )}
        {currentTab === 'tab3' && (
          <Box m={2}>
            <Typography variant="h2">{'YugabyteDB Managed'}</Typography>
            <Box mt={1}>
              <Typography variant="body2">
                {
                  'A fully-managed YugabyteDB-as-a-Service running on Amazon Web Services or Google Cloud Platform.'
                }
              </Typography>
            </Box>
          </Box>
        )}
      </Box>
    </Box>
  );
};
