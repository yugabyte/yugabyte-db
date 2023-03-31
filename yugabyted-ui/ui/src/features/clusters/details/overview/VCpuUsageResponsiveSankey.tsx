import React, { FC } from 'react';
import { Link, makeStyles } from '@material-ui/core';
import { ResponsiveContainer } from 'recharts';
import type { ClusterData } from '@app/api/src';
import { Link as RouterLink } from 'react-router-dom';
import { VCpuUsageSankey } from './VCpuUsageSankey';

const useStyles = makeStyles((theme) => ({
  container: {
    height: '186px',
    overflow: 'auto',
    padding: theme.spacing(1, 0, 0.5, 0),
  },
  link: {
    '&:link, &:focus, &:active, &:visited, &:hover': {
      textDecoration: 'none',
      color: theme.palette.text.primary,
    }
  },
}));

interface VCpuUsageResponsiveSankeyProps {
  cluster: ClusterData,
}

export const VCpuUsageResponsiveSankey: FC<VCpuUsageResponsiveSankeyProps> = ({ cluster }) => {
  const classes = useStyles();

  return (
    <div className={classes.container}>
      <Link className={classes.link} component={RouterLink} to="/performance/metrics">
        <ResponsiveContainer width="99%" height="100%" debounce={2} minWidth={380}>
          <VCpuUsageSankey cluster={cluster} sankeyProps={{ cursor: 'pointer' }} />
        </ResponsiveContainer>
      </Link>
    </div>
  );
};