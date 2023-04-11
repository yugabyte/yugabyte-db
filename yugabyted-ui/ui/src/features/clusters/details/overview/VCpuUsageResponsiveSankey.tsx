import React, { FC } from 'react';
import { makeStyles } from '@material-ui/core';
import { ResponsiveContainer } from 'recharts';
import type { ClusterData } from '@app/api/src';
import { VCpuUsageSankey } from './VCpuUsageSankey';

const useStyles = makeStyles((theme) => ({
  container: {
    height: '186px',
    overflow: 'auto',
    padding: theme.spacing(1, 0, 0.5, 0),
  },
}));

interface VCpuUsageResponsiveSankeyProps {
  cluster: ClusterData,
}

export const VCpuUsageResponsiveSankey: FC<VCpuUsageResponsiveSankeyProps> = ({ cluster }) => {
  const classes = useStyles();

  return (
    <div className={classes.container}>
      <ResponsiveContainer width="99%" height="100%" debounce={2} minWidth={420}>
        <VCpuUsageSankey cluster={cluster} />
      </ResponsiveContainer>
    </div>
  );
};