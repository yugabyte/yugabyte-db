import React, { FC } from 'react';
import { Box } from '@material-ui/core';

import { useGetVersionQuery, } from '@app/api/src';
import { ClusterDBVersionBadge } from '@app/features/clusters/ClusterDBVersionBadge';
//import { ClusterDBVersionReleaseBadge } from '@app/features/clusters/ClusterDBVersionReleaseBadge'

export const VersionBadge: FC = () => {
  const { data: versionInfo } = useGetVersionQuery();
  const software_version = versionInfo?.version ?? '';
  return (
    <>
      <Box ml={1} hidden={!software_version}>
        <ClusterDBVersionBadge text={`v${software_version}`}/>
      </Box>
      {/* <Box ml={1} hidden={!software_version}>
        <ClusterDBVersionReleaseBadge text={`v${software_version}`}/>
      </Box> */}
    </>
  );
};
