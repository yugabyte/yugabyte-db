import React, { FC, useMemo } from 'react';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBTable, YBLoadingBox, YBStatus, STATUS_TYPES } from '@app/components';
import { getMemorySizeUnits, roundDecimal } from '@app/helpers';
import { useGetClusterNodesQuery } from '@app/api/src';
import { getHumanVersion } from '@app/features/clusters/ClusterDBVersionBadge';

const StatusComponent = (isHealthy: boolean) => (
  <YBStatus type={isHealthy ? STATUS_TYPES.SUCCESS : STATUS_TYPES.FAILED} />
);
const useStyles = makeStyles((theme) => ({
  loadingCount: {
    height: theme.spacing(2.5),
    width: theme.spacing(20),
    background: theme.palette.grey[200]
  },
  loadingBox: {
    height: theme.spacing(11),
    width: '100%',
    background: theme.palette.grey[200],
    borderRadius: theme.shape.borderRadius
  }
}));

export const NodesTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  //Get nodes
  const { data: nodesResponse, isLoading: fetchingNodes } = useGetClusterNodesQuery();

  const nodesData = useMemo(() => {
    if (nodesResponse?.data) {
      return nodesResponse.data.map((node) => ({
        ...node,
        status: node.is_node_up,
        name: node.name,
        is_master: node.is_master,
        is_tserver: node.is_tserver,
        cloud: node.cloud_info.cloud,
        region: node.cloud_info.region,
        zone: node.cloud_info.zone,
        software_version: node.software_version ? getHumanVersion(node.software_version) : '-',
        ram_used: getMemorySizeUnits(node.metrics?.memory_used_bytes),
        sst_size: getMemorySizeUnits(node.metrics?.total_sst_file_size_bytes),
        uncompressed_sst_size: getMemorySizeUnits(node.metrics?.uncompressed_sst_file_size_bytes),
        read_write_ops: node.metrics
          ? `${roundDecimal(node.metrics.read_ops_per_sec)} | ${roundDecimal(node.metrics.write_ops_per_sec)}`
          : '-'
      }));
    }
    return [];
  }, [nodesResponse]);

  if (fetchingNodes) {
    return (
      <>
        <Box mt={3} mb={2.5}>
          <div className={classes.loadingCount} />
        </Box>
        <div className={classes.loadingBox} />
      </>
    );
  }

  const NODES_TABLE_COLUMNS = [
    {
      name: 'status',
      options: {
        filter: true,
        hideHeader: true,
        setCellProps: () => ({ style: { width: '30px', maxWidth: '30px', paddingRight: 0 } }),
        customBodyRender: StatusComponent
      }
    },
    {
      name: 'name',
      label: t('clusterDetail.nodes.name'),
      options: {
        filter: true
      }
    },
    {
      name: 'cloud',
      label: t('clusterDetail.nodes.cloud'),
      options: {
        filter: true
      }
    },
    {
      name: 'region',
      label: t('clusterDetail.nodes.region'),
      options: {
        filter: true
      }
    },
    {
      name: 'zone',
      label: t('clusterDetail.nodes.zone'),
      options: {
        filter: true
      }
    },
    {
      name: 'software_version',
      label: t('clusterDetail.nodes.version'),
      options: {
        filter: true
      }
    },
    {
      name: 'ram_used',
      label: t('clusterDetail.nodes.ramUsed'),
      options: {
        filter: true,
        setCellProps: () => ({ style: { width: '140px' } })
      }
    },
    {
      name: 'sst_size',
      label: t('clusterDetail.nodes.sstSize'),
      options: {
        filter: true,
        setCellProps: () => ({ style: { width: '140px' } })
      }
    },
    {
      name: 'uncompressed_sst_size',
      label: t('clusterDetail.nodes.uncompressedSstSize'),
      options: {
        filter: true,
        setCellProps: () => ({ style: { width: '200px' } })
      }
    },
    {
      name: 'read_write_ops',
      label: t('clusterDetail.nodes.readWriteOpsPerSec'),
      options: {
        filter: true,
        setCellProps: () => ({ style: { width: '160px' } })
      }
    }
  ];

  return nodesData.length ? (
    <>
      <Box mt={3} mb={2}>
        <Typography variant="h5">{t('clusterDetail.nodes.numNodes', { count: nodesData.length })}</Typography>
      </Box>
      <Box pb={4} pt={1}>
        <YBTable data={nodesData} columns={NODES_TABLE_COLUMNS} options={{ pagination: false }} />
      </Box>
    </>
  ) : (
    <YBLoadingBox>{t('clusterDetail.nodes.noNodesCopy')}</YBLoadingBox>
  );
};
