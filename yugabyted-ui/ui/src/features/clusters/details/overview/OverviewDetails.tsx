import React, { FC } from 'react';
import { Box } from '@material-ui/core';
import { OverviewTab } from '@app/features/clusters/details/overview/OverviewTab';

export const OverviewDetails: FC = () => {
  return (
    <Box p={2}>
      <OverviewTab/>
    </Box>
  );
};
