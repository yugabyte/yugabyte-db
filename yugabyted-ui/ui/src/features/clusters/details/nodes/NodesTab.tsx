import React, { FC } from 'react';
import { Box } from '@material-ui/core';
import { NodesList } from './NodesList';

export const NodesTab: FC = () => {

  return (
    <Box>
      <NodesList />
      {/* <ReadReplicaList /> */}
    </Box>
  )
};
