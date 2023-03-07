import React, { FC, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { makeStyles, Paper, Typography } from '@material-ui/core';
import { YBTable } from '@app/components';

const useStyles = makeStyles((theme) => ({
  paperContainer: {
    padding: theme.spacing(3),
    paddingBottom: theme.spacing(4),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: '100%'
  },
  heading: {
    marginBottom: theme.spacing(5),
  },
}));

interface GFlagsOverviewProps {
}

export const GFlagsOverview: FC<GFlagsOverviewProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const gflagData = useMemo(() => [
    {
      flag: 'raft_heartbeat_interval',
      master: '500',
      tserver: '-',
    },
    {
      flag: 'leader_failure_heartbeat_RTO',
      master: '2500',
      tserver: '-',
    },
    {
      flag: 'log_rentention_duration',
      master: '-',
      tserver: '5000',
    },
  ], []);

  const gflagColumns = [
    {
      name: 'flag',
      label: t('clusterDetail.settings.gflags.flag'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
    {
      name: 'master',
      label: t('clusterDetail.settings.gflags.masterValue'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
    {
      name: 'tserver',
      label: t('clusterDetail.settings.gflags.tserverValue'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
  ];

  return (
    <Paper className={classes.paperContainer}>
      <Typography variant="h4" className={classes.heading}>
        {t('clusterDetail.settings.gflags.title')}
      </Typography>
      <YBTable
        data={gflagData}
        columns={gflagColumns}
        options={{ pagination: false }}
        withBorder={false}
      />
    </Paper>
  );
};
