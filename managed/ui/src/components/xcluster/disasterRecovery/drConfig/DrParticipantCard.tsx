import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useQuery } from 'react-query';
import { Link } from 'react-router';

import { api, universeQueryKey } from '../../../../redesign/helpers/api';
import { YBLoadingCircleIcon } from '../../../common/indicators';
import { UniverseXClusterRole } from '../../constants';
import { UniverseDrStateLabel } from '../UniverseDrStateLabel';
import { getPlacementRegions } from '../../../../utils/UniverseUtils';
import { getPrimaryCluster } from '../../../../utils/universeUtilsTyped';

import { XClusterConfig } from '../../dtos';
import { PlacementRegion } from '../../../../redesign/helpers/dtos';

interface DrParticipantCardProps {
  xClusterConfig: XClusterConfig;
  universeXClusterRole: UniverseXClusterRole;

  width?: number;
}

const useStyles = makeStyles((theme) => ({
  drParticipant: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.5),

    padding: theme.spacing(2),
    minWidth: (props: { width?: number }) => (props.width ? `${props.width}px` : '480px'),
    height: '80px',

    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px',
    background: theme.palette.ybacolors.backgroundGrayLight
  },
  universeLink: {
    color: theme.palette.common.black,
    '&:hover': {
      color: theme.palette.orange[500]
    }
  }
}));

export const DrParticipantCard = ({
  xClusterConfig,
  universeXClusterRole,
  width
}: DrParticipantCardProps) => {
  const classes = useStyles({ width });
  const theme = useTheme();

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

  // Read replica tservers get their updates from the target universe voter quorum rather than the source voter quorum.
  // Thus, for xCluster/DR it doesn't make sense to attach the read replica clusters on the UI for primary cluster xCluster replication.
  const primaryClusterRegionList: string[] = getPlacementRegions(
    getPrimaryCluster(universeQuery.data?.universeDetails.clusters ?? [])
  ).map((region: PlacementRegion) => region.code);
  const primaryClusterRegionListText = primaryClusterRegionList?.join(', ') ?? '';
  const universeName = universeQuery.data?.name ?? universeUuid;
  return (
    <div className={classes.drParticipant}>
      <Box display="flex" gridGap={theme.spacing(0.5)}>
        {universeQuery.isLoading ? (
          <YBLoadingCircleIcon />
        ) : (
          <>
            <Typography variant="h5">
              <Link to={`/universes/${universeUuid}`} className={classes.universeLink}>
                {universeName}
              </Link>
            </Typography>
          </>
        )}
        <Box marginLeft="auto">
          <UniverseDrStateLabel
            xClusterConfig={xClusterConfig}
            universeXClusterRole={universeXClusterRole}
          />
        </Box>
      </Box>
      <Typography variant="body2" component="div">
        {primaryClusterRegionListText}
      </Typography>
    </div>
  );
};
