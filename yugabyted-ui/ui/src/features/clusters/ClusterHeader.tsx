import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';

import { useGetClusterQuery, } from '@app/api/src';
import { ClusterDBVersionBadge } from '@app/features/clusters/ClusterDBVersionBadge';
import { ClusterDBVersionReleaseBadge } from '@app/features/clusters/ClusterDBVersionReleaseBadge';

export const ClusterHeader: FC = () => {
  const { t } = useTranslation();
  const { data: clusterData } = useGetClusterQuery();
  const software_version = clusterData?.data?.info?.software_version ?? '';
  return (
    <>
      <Typography variant="h4">{t('common.cluster')}</Typography>
      <Box ml={1} hidden={!software_version}>
        <ClusterDBVersionBadge text={`v${software_version}`}/>
      </Box>
      <Box ml={1} hidden={!software_version}>
        <ClusterDBVersionReleaseBadge text={`v${software_version}`}/>
      </Box>
    </>
  );
};
