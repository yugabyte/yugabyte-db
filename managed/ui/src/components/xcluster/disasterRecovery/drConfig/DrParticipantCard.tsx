import { Box, makeStyles, Typography } from '@material-ui/core';
import { useQuery } from 'react-query';

import { api, universeQueryKey } from '../../../../redesign/helpers/api';
import { YBLoadingCircleIcon } from '../../../common/indicators';
import { UniverseXClusterRole } from '../../constants';
import { UniverseDrStateLabel } from '../UniverseDrStateLabel';

import { XClusterConfig } from '../../dtos';

interface DrParticipantCardProps {
  xClusterConfig: XClusterConfig;
  universeXClusterRole: UniverseXClusterRole;
}

const useStyles = makeStyles((theme) => ({
  drParticipant: {
    display: 'flex',
    gap: theme.spacing(0.5),

    padding: theme.spacing(2),
    width: '480px',
    height: '80px',

    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px',
    background: theme.palette.ybacolors.backgroundGrayLight
  }
}));

export const DrParticipantCard = ({
  xClusterConfig,
  universeXClusterRole
}: DrParticipantCardProps) => {
  const classes = useStyles();

  const universeUuid =
    universeXClusterRole === UniverseXClusterRole.SOURCE
      ? xClusterConfig.sourceUniverseUUID
      : xClusterConfig.targetUniverseUUID;
  const universeQuery = useQuery(
    universeQueryKey.detail(universeUuid),
    () => api.fetchUniverse(universeUuid),
    { enabled: universeUuid !== undefined }
  );

  if (universeUuid === undefined) {
    return <div className={classes.drParticipant} />;
  }

  const universeName = universeQuery.data?.name ?? universeUuid;
  return (
    <div className={classes.drParticipant}>
      {universeQuery.isLoading ? (
        <YBLoadingCircleIcon />
      ) : (
        <>
          <Typography variant="h5">{universeName}</Typography>
        </>
      )}
      <Box marginLeft="auto">
        <UniverseDrStateLabel
          xClusterConfig={xClusterConfig}
          universeXClusterRole={universeXClusterRole}
        />
      </Box>
    </div>
  );
};
