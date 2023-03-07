import React, { FC, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Paper, Typography } from '@material-ui/core';
import { countryToFlag } from '@app/helpers';
import { YBTable } from '@app/components';
import type { MUISortOptions } from 'mui-datatables';

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

  /* const { data: clusterData } = useGetClusterQuery();
  const cluster = clusterData?.data;
  const clusterSpec = cluster?.spec;

  const regionData = clusterSpec?.cluster_region_info?.map((region, index) => ({
    region: {
      name: region.placement_info.cloud_info.region,
      code: region.placement_info.cloud_info.code,
    },
    nodeCount: region.placement_info.num_nodes,
    vCpuPerNode: '4',
    ramPerNode: '8 GB',
    diskPerNode: '120 GB',
    vpc: 'us-west-production',
  })) ?? []; */

  const regionData = useMemo(() => [
    {
      region: {
        name: 'N. Virginia (us-east-1)',
        code: 'us',
      },
      nodeCount: '3',
      vCpuPerNode: '4',
      ramPerNode: '8 GB',
      diskPerNode: '120 GB',
      vpc: 'us-west-production',
    },
    {
      region: {
        name: 'N. California (us-west-1)',
        code: 'us',
      },
      nodeCount: '3',
      vCpuPerNode: '4',
      ramPerNode: '8 GB',
      diskPerNode: '120 GB',
      vpc: 'eu-central-production',
    },
    {
      region: {
        name: 'Mumbai (ap-south-1)',
        code: 'in',
      },
      nodeCount: '3',
      vCpuPerNode: '4',
      ramPerNode: '8 GB',
      diskPerNode: '120 GB',
      vpc: 'ap-southeast-production',
    }
  ], []);

  const regionColumns = [
    {
      name: 'region',
      label: t('clusterDetail.settings.regions.region'),
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
    {
      name: 'vpc',
      label: t('clusterDetail.settings.regions.vpcName'),
      options: {
        setCellProps: () => ({ style: { padding: '8px 0px' } }),
        setCellHeaderProps: () => ({ style: { padding: '8px 0px' } }),
      }
    },
  ];

  return (
    <Paper className={classes.paperContainer}>
      <Typography variant="h5" className={classes.heading}>
        {t('clusterDetail.settings.regions.title')}
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
