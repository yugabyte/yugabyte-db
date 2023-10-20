import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Divider, Grid, Link, makeStyles, Typography } from '@material-ui/core';
import { Link as RouterLink } from 'react-router-dom';
import type { HealthCheckInfo } from '@app/api/src';
import { ChevronRight } from '@material-ui/icons';
import clsx from 'clsx';
import { ClusterTabletWidget } from './ClusterTabletWidget';
import { STATUS_TYPES, YBStatus } from '@app/components';
import { useNodes } from '../nodes/NodeHooks';

const useStyles = makeStyles((theme) => ({
  divider: {
    width: '100%',
    marginLeft: 0,
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  container: {
    flexWrap: "nowrap",
    marginTop: theme.spacing(1.5),
    justifyContent: 'space-between',
  },
  section: {
    marginRight: theme.spacing(2),
  },
  sectionBorder: {
    paddingLeft: theme.spacing(2),
    borderLeft: `1px solid ${theme.palette.grey[300]}`
  },
  title: {
    color: theme.palette.grey[900],
    fontWeight: theme.typography.fontWeightRegular as number,
    flexGrow: 1,
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginTop: theme.spacing(1.25),
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase',
    textAlign: 'left'
  },
  value: {
    color: theme.palette.grey[900],
    textAlign: 'left',
    paddingTop: theme.spacing(0.25)
  },
  arrow: {
    color: theme.palette.grey[600],
    marginTop: theme.spacing(0.5)
  },
  loadingCount: {
    height: theme.spacing(2.5),
    width: theme.spacing(2.5),
    background: theme.palette.grey[200]
  },
  link: {
    '&:link, &:focus, &:active, &:visited, &:hover': {
      textDecoration: 'none',
      color: theme.palette.text.primary
    }
  }
}));

interface ClusterNodeWidgetProps {
  health: HealthCheckInfo;
}

export const ClusterNodeWidget: FC<ClusterNodeWidgetProps> = ({ health }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  // Get nodes
  const { data: nodesResponse, isFetching: fetchingNodes } = useNodes();

  const nodesData = nodesResponse?.data;

  const numNodes = nodesData?.length ?? 0;
  const deadNodes = nodesData?.filter(node => !node.is_node_up) ?? [];
  const bootstrappingNodes = nodesData?.filter(node => node.is_bootstrapping) ?? [];
  const numHealthyNodes = numNodes - deadNodes.length;

  return (
    <Box>
      <Link className={classes.link} component={RouterLink} to="?tab=tabNodes">
        <Box display="flex" alignItems="center">
          <Typography variant="body2" className={classes.title}>{t('clusterDetail.overview.nodes')}</Typography>
          <ChevronRight className={classes.arrow} />
        </Box>
        <Grid container className={classes.container}>
          <div className={classes.section}>
            <Typography variant="h4" className={classes.value}>
              {fetchingNodes ? <div className={classes.loadingCount} /> : numNodes}
            </Typography>
            <Typography variant="body2" className={classes.label}>
              {t('clusterDetail.nodes.total')}
            </Typography>
          </div>
          <Link className={classes.link} component={RouterLink} to="?tab=tabNodes&filter=running">
            <div className={clsx(classes.section, classes.sectionBorder)}>
              <Box display="flex" gridGap={7}>
                <Typography variant="h4" className={classes.value}>
                  {fetchingNodes ? <div className={classes.loadingCount} /> : numHealthyNodes}
                </Typography>
                <YBStatus value={fetchingNodes ? 0 : numHealthyNodes} type={STATUS_TYPES.SUCCESS} tooltip />
              </Box>
              <Typography variant="body2" className={classes.label}>
                {t('clusterDetail.nodes.running')}
              </Typography>
            </div>
          </Link>
          <Link className={classes.link} component={RouterLink} to="?tab=tabNodes&filter=down">
            <div className={classes.section}>
              <Box display="flex" gridGap={7}>
                <Typography variant="h4" className={classes.value}>
                  {fetchingNodes ? <div className={classes.loadingCount} /> : deadNodes.length}
                </Typography>
                <YBStatus value={fetchingNodes ? 0 : deadNodes.length} type={STATUS_TYPES.FAILED} tooltip />
              </Box>
              <Typography variant="body2" className={classes.label}>
                {t('clusterDetail.nodes.down')}
              </Typography>
            </div>
          </Link>
          <Link className={classes.link} component={RouterLink} to="?tab=tabNodes&filter=bootstrapping">
            <div className={clsx(classes.section, classes.sectionBorder)}>
              <Box display="flex" gridGap={7}>
                <Typography variant="h4" className={classes.value}>
                  {fetchingNodes ? <div className={classes.loadingCount} /> : bootstrappingNodes.length}
                </Typography>
                <YBStatus value={fetchingNodes ? 0 : bootstrappingNodes.length} type={STATUS_TYPES.IN_PROGRESS} tooltip />
              </Box>
              <Typography variant="body2" className={classes.label}>
                {t('clusterDetail.nodes.bootstrapping')}
              </Typography>
            </div>
          </Link>
        </Grid>
      </Link>
      <Divider orientation="horizontal" variant="middle" className={classes.divider} />
      <Link className={classes.link} component={RouterLink} to="/databases/tabYsql">
        <ClusterTabletWidget health={health} />
      </Link>
    </Box>
  );
};
