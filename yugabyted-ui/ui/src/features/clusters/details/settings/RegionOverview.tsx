import React, { FC, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Paper, Typography } from '@material-ui/core';
import { countryToFlag, getRegionCode, roundDecimal } from '@app/helpers';
import { YBTable } from '@app/components';
import type { MUISortOptions } from 'mui-datatables';
import { ClusterFaultTolerance, useGetClusterNodesQuery, useGetClusterQuery } from '@app/api/src';

const useStyles = makeStyles((theme) => ({
  paperContainer: {
    padding: theme.spacing(3),
    paddingBottom: theme.spacing(4),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: '100%'
  },
  summaryContainer: {
    display: 'flex',
    gap: '10px',
    margin: theme.spacing(3, 0, 2, 0),
    justifyContent: 'space-between',
    padding: theme.spacing(2, 6, 2, 3),
    borderRadius: theme.shape.borderRadius,
    border: '1px solid',
    borderColor: theme.palette.grey[100],
  },
  summaryItem: {
    display: 'flex',
  },
  summaryTitle: {
    marginRight: theme.spacing(4),
    textTransform: 'uppercase',
    color: theme.palette.grey[600],
  }
}));

const RegionNameComponent = () => ({ name, code }: { name: string, code?: string }) => {
  return (
    <Box>
      {countryToFlag(code)} {name}
    </Box>
  );
}

interface RegionOverviewProps {
  readReplica?: boolean,
}

export const RegionOverview: FC<RegionOverviewProps> = ({ readReplica }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: clusterData } = useGetClusterQuery();
  const cluster = clusterData?.data;
  const clusterSpec = cluster?.spec;

  const isZone = clusterSpec?.cluster_info.fault_tolerance === ClusterFaultTolerance.Zone;

  // Get text for ram usage
  const getRamUsageText = (ramUsageGb: number) => {
    ramUsageGb = roundDecimal(ramUsageGb)
    return t('units.GB', { value: ramUsageGb });
  }

  // Get text for disk usage
  const getDiskSizeText = (diskSizeGb: number) => {
    diskSizeGb = roundDecimal(diskSizeGb)
    return t('units.GB', { value: diskSizeGb });
  }

  // Get nodes
  const { data: nodesResponse } = useGetClusterNodesQuery({});
  const nodeList = React.useMemo(() =>
    nodesResponse?.data.filter(node => (readReplica && node.is_read_replica) || (!readReplica && !node.is_read_replica))
  ,[nodesResponse, readReplica]);

  const totalCores = roundDecimal((clusterSpec?.cluster_info?.node_info.num_cores ?? 0) / (nodesResponse?.data.length ?? 1) * (nodeList?.length ?? 0));
  const totalDiskSize = roundDecimal((clusterSpec?.cluster_info.node_info.disk_size_gb ?? 0) / (nodesResponse?.data.length ?? 1) * (nodeList?.length ?? 0));
  const totalRamProvisionedGb = (clusterSpec?.cluster_info?.node_info.ram_provisioned_gb ?? 0);

  const regionData = useMemo(() => {
    const set = new Set<string>();
    nodeList?.forEach(node => set.add(node.cloud_info.region + "#" + node.cloud_info.zone));

    const isLocalCluster = !!nodeList?.find(node => node.host.startsWith("127."));
    const divideFactor = isLocalCluster ? 1 : (nodeList?.length ?? 1);

    return Array.from(set).map(regionZone => {
      const [region, zone] = regionZone.split('#');
      return {
        region: {
          name: `${region} (${zone})`,
          code: getRegionCode({ region, zone }),
        },
        nodeCount: nodeList?.filter(node =>
          node.cloud_info.region === region && node.cloud_info.zone === zone).length,
        vCpuPerNode: totalCores / divideFactor,
        ramPerNode: getRamUsageText(totalRamProvisionedGb / divideFactor),
        diskPerNode: getDiskSizeText(totalDiskSize / divideFactor),
      }
    })
  }, [nodeList, totalCores, totalRamProvisionedGb, totalDiskSize, readReplica])

  const summaryData = useMemo(() => [
    {
      title: t('clusterDetail.settings.regions.totalNodes'),
      value: nodeList?.filter(node => (readReplica && node.is_read_replica) || (!readReplica && !node.is_read_replica))
        .length.toString(),
    },
    {
      title: t('clusterDetail.settings.regions.totalvCPU'),
      value: totalCores.toString(),
    },
    {
      title: t('clusterDetail.settings.regions.totalMemory'),
      value: getRamUsageText(totalRamProvisionedGb),
    },
    {
      title: t('clusterDetail.settings.regions.totalDiskSize'),
      value: getDiskSizeText(totalDiskSize),
    }
  ], [nodeList, totalCores, totalRamProvisionedGb, totalDiskSize])

  const regionColumns = [
    {
      name: 'region',
      label: isZone ? t('clusterDetail.settings.regions.zone') : t('clusterDetail.settings.regions.region'),
      options: {
        customBodyRender: RegionNameComponent(),
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      },
      customColumnSort: (order: MUISortOptions['direction']) => {
        return (obj1: { data: any }, obj2: { data: any }) => {
          let val1 = obj1.data.name;
          let val2 = obj2.data.name;
          let compareResult =
            val2 < val1
              ? 1
              : val2 == val1
                ? 0
                : -1;
          return compareResult * (order === 'asc' ? 1 : -1);
        };
      },
    },
    {
      name: 'nodeCount',
      label: t('clusterDetail.settings.regions.nodes'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
    {
      name: 'vCpuPerNode',
      label: t('clusterDetail.settings.regions.vCPU_per_node'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
    {
      name: 'ramPerNode',
      label: t('clusterDetail.settings.regions.ram_per_node'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
    {
      name: 'diskPerNode',
      label: t('clusterDetail.settings.regions.disk_per_node'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
  ];

  if (regionData.length === 0) {
    return null;
  }

  return (
    <Paper className={classes.paperContainer}>
      <Typography variant="h4">
        {readReplica ? t('clusterDetail.settings.regions.readReplica') : t('clusterDetail.settings.regions.title')}
      </Typography>
      <Box className={classes.summaryContainer}>
        {summaryData.map(summaryItem => (
          <Box key={summaryItem.title} className={classes.summaryItem}>
            <Typography variant='body2' className={classes.summaryTitle}>{summaryItem.title}</Typography>
            <Typography variant='body2'>{summaryItem.value}</Typography>
          </Box>
        ))}
      </Box>
      <YBTable
        data={regionData}
        columns={regionColumns}
        options={{ pagination: false }}
        withBorder={false}
      />
    </Paper>
  );
};
