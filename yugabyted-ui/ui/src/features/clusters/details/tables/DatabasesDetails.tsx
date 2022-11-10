import React, { FC } from 'react';
import { Box } from '@material-ui/core';
import { TablesTab } from '@app/features/clusters/details/tables/TablesTab';

export const DatabasesDetails: FC = () => {
  return (
    <Box p={2}>
      <TablesTab/>
    </Box>
  );
};
