import React, { FC, useEffect, useMemo, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Paper, Typography } from '@material-ui/core';
import { YBTable } from '@app/components';

const useStyles = makeStyles((theme) => ({
  paperContainer: {
    padding: theme.spacing(3),
    paddingBottom: theme.spacing(4),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: '100%'
  },
  heading: {
    display: "flex",
    alignItems: "center",
    gap: theme.spacing(2),
  },
  subHeading: {
    marginTop: theme.spacing(5),
    marginBottom: theme.spacing(1.5),
  },
  selectBox: {
    marginTop: theme.spacing(-0.5),
    minWidth: '180px',
  },
}));

interface GFlagsDrfitProps {
}

export const GFlagsDrift: FC<GFlagsDrfitProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const paperRef = useRef<HTMLDivElement>();
  useEffect(() => paperRef.current?.scrollIntoView({ behavior: 'smooth' }), [])

  React.useEffect(() => {
  }, [])

  const gflagMasterDrift = useMemo(() => [
    {
      flag: 'raft_heartbeat_interval',
      nodes: 'Node A, Node B',
      driftNodes: 'Node C',
    },
    {
      flag: 'leader_failure_heartbeat_RTO',
      nodes: 'Node A, Node B, Node C',
      driftNodes: '-',
    },
    {
      flag: 'log_rentention_duration',
      nodes: 'Node B, Node C',
      driftNodes: 'Node A',
    },
  ], []);

  const gflagTserverDrift = useMemo(() => [
    {
      flag: 'raft_heartbeat_interval',
      nodes: 'Node A, Node C',
      driftNodes: 'Node B',
    },
    {
      flag: 'leader_failure_heartbeat_RTO',
      nodes: 'Node A, Node B, Node C',
      driftNodes: '-',
    },
    {
      flag: 'log_rentention_duration',
      nodes: 'Node A, Node B, Node C',
      driftNodes: '-',
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
      name: 'nodes',
      label: t('clusterDetail.settings.gflags.nodesWithoutDrift'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
    {
      name: 'driftNodes',
      label: t('clusterDetail.settings.gflags.nodesWithDrift'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
  ];

  return (
    <Paper className={classes.paperContainer} ref={paperRef}>
      <Box className={classes.heading}>
        <Typography variant="h4">
          {t('clusterDetail.settings.gflags.driftTitle')}
        </Typography>
      </Box>

      <Typography variant="h5" className={classes.subHeading}>
        {t('clusterDetail.settings.gflags.masterFlags')}
      </Typography>
      <YBTable
        data={gflagMasterDrift}
        columns={gflagColumns}
        options={{ pagination: false }}
        withBorder={false}
      />

      <Typography variant="h5" className={classes.subHeading}>
        {t('clusterDetail.settings.gflags.tserverFlags')}
      </Typography>
      <YBTable
        data={gflagTserverDrift}
        columns={gflagColumns}
        options={{ pagination: false }}
        withBorder={false}
      />
    </Paper>
  );
};
