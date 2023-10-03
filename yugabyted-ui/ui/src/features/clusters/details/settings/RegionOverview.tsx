import React, { FC, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Paper, Typography } from '@material-ui/core';
import { countryToFlag, roundDecimal } from '@app/helpers';
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
  heading: {
    marginBottom: theme.spacing(5),
  },
}));

const regionCountryCodes: { [k: string]: string } = {
  'us-east-2':      'us',
  'us-east-1':      'us',
  'us-west-1':      'us',
  'us-west-2':      'us',
  'af-south-1':     'za',
  'ap-south-1':     'in',
  'ap-northeast-3': 'jp',
  'ap-southeast-1': 'sg',
  'ap-southeast-2': 'au',
  'ap-northeast-1': 'jp',
  'ca-central-1':   'ca',
  'eu-central-1':   'de',
  'eu-west-1':      'ie',
  'eu-west-2':      'gb',
  'eu-south-1':     'it',
  'eu-west-3':      'fr',
  'eu-north-1':     'se',
  'me-south-1':     'bh',
  'sa-east-1':      'br',
  'us-gov-east-1':  'us',
  'us-gov-west-1':  'us',
}

const RegionNameComponent = () => ({ name, code }: { name: string, code?: string }) => {
  return (
    <Box>
      {countryToFlag(code)} {name}
    </Box>
  );
}

interface RegionOverviewProps {
}

export const RegionOverview: FC<RegionOverviewProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: clusterData } = useGetClusterQuery();
  const cluster = clusterData?.data;
  const clusterSpec = cluster?.spec;

  const isZone = clusterSpec?.cluster_info.fault_tolerance === ClusterFaultTolerance.Zone;
  const totalCores = clusterSpec?.cluster_info?.node_info.num_cores ?? 0;
  const totalDiskSize = clusterSpec?.cluster_info.node_info.disk_size_gb ?? 0;

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
  const { data: nodesResponse } = useGetClusterNodesQuery();
  const totalRamProvisionedGb = clusterSpec?.cluster_info?.node_info.ram_provisioned_gb ?? 0;

  const regionData = useMemo(() => {
    const set = new Set<string>();
    nodesResponse?.data.forEach(node => set.add(node.cloud_info.region + "#" + node.cloud_info.zone));
    return Array.from(set).map(regionZone => {
      const [region, zone] = regionZone.split('#');
      return {
        region: {
          name: `${region} (${zone})`,
          code: Object.entries(regionCountryCodes).find(([key]) => zone.startsWith(key) || key.startsWith(region))?.[1],
        },
        nodeCount: nodesResponse?.data.filter(node => 
          node.cloud_info.region === region && node.cloud_info.zone === zone).length,
        vCpuPerNode: totalCores / (nodesResponse?.data.length ?? 1),
        ramPerNode: getRamUsageText(totalRamProvisionedGb / (nodesResponse?.data.length ?? 1)),
        diskPerNode: getDiskSizeText(totalDiskSize / (nodesResponse?.data.length ?? 1)),
      }
    })
  }, [nodesResponse, totalCores, totalRamProvisionedGb, totalDiskSize])

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

  return (
    <Paper className={classes.paperContainer}>
      <Typography variant="h4" className={classes.heading}>
        {isZone ? t('clusterDetail.settings.regions.zonesTitle') : t('clusterDetail.settings.regions.title')}
      </Typography>
      <YBTable
        data={regionData}
        columns={regionColumns}
        options={{ pagination: false }}
        withBorder={false}
      />
    </Paper>
  );
};
