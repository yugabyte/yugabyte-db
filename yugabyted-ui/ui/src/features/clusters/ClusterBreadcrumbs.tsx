import React, { FC } from 'react';
// import { Link, useParams } from 'react-router-dom';
import { useTranslation } from 'react-i18next';
// import { Box, Breadcrumbs, makeStyles, Typography } from '@material-ui/core';
import { Typography } from '@material-ui/core';

// import { useGetClusterQuery, useListTracksForAccountQuery } from '@app/api/src';
// import { ClusterTierBadge, showTierBadge } from '@app/features/clusters/ClusterTierBadge';
// import { ClusterDBVersionBadge } from '@app/features/clusters/ClusterDBVersionBadge';
// import CaretRightIcon from '@app/assets/caret-right.svg';

// const useStyles = makeStyles((theme) => ({
//   link: {
//     textDecoration: 'none',
//     textDecorationColor: theme.palette.primary.main,
//     '&:hover': {
//       textDecoration: 'underline',
//       textDecorationColor: theme.palette.primary.main
//     }
//   }
// }));

export const ClusterBreadcrumbs: FC = () => {
//   const classes = useStyles();
  const { t } = useTranslation();
//   const { accountId, projectId, clusterId } = useParams<App.RouteParams>();

//   const { data: clusterData } = useGetClusterQuery(
//     {
//       query: {
//         // skip clusters list and new cluster wizard pages
//         enabled: !!clusterId && clusterId !== 'new'
//       }
//     }
//   );

//   //Release Tracks
//   const { data: releaseTracks } = useListTracksForAccountQuery({
//     accountId
//   });
//   const releaseTrack = releaseTracks?.data?.find(
//     ({ info: releaseInfo }) =>
//       releaseInfo?.id === clusterData?.data?.spec?.software_info?.track_id
//   );
//   //Release Tracks

//   const clusterSpec = clusterData?.data?.spec;
//   const clusterName = clusterSpec?.name ?? '';
//   const clusterTier = clusterSpec?.cluster_info.cluster_tier ?? '';
//   const software_version = clusterData?.data?.info?.software_version ?? '';
//   const releaseName =
//     releaseTrack?.spec?.name ? `${releaseTrack?.spec?.name} ${t('clusters.track')}` : '';

//   if (clusterName) {
//     return (
//       <>
//         <Breadcrumbs separator={<CaretRightIcon />} aria-label="breadcrumb">
//           <Link to={`/a/${accountId}/p/${projectId}/clusters`} className={classes.link}>
//             <Typography variant="h4" color="primary">
//               {t('common.clusters')}
//             </Typography>
//           </Link>
//           <Typography variant="h4" color="textPrimary">
//             {clusterName}
//           </Typography>
//         </Breadcrumbs>
//         <Box ml={1} hidden={!software_version}>
//           <ClusterDBVersionBadge text={`v${software_version}`} tooltipMsg={releaseName} />
//         </Box>
//         {clusterTier && (
//           <Box ml={0.5} hidden={!showTierBadge(clusterTier)}>
//             <ClusterTierBadge tier={clusterTier} />
//           </Box>
//         )}
//         <Box ml={0.5}>
//         </Box>
//       </>
//     );
//   }

  return <Typography variant="h4">{t('common.clusters')}</Typography>;
};
