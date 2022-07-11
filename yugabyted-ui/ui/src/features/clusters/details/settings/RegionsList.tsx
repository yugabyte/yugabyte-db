import React, { FC, useCallback, useContext } from 'react';
import { Box, makeStyles, Paper, Typography, Link as MUILink } from '@material-ui/core';
import { Link, useParams } from 'react-router-dom';
import type { MUIDataTableMeta } from 'mui-datatables';
import { useTranslation } from 'react-i18next';

// Local imports
import { YBButton, YBTable } from '@app/components';
import { ClusterData, ClusterTier, RegionListResponse, useListSingleTenantVpcsQuery } from '@app/api/src';
import { convertMBtoGB, OPEN_EDIT_INFRASTRUCTURE_MODAL } from '@app/helpers';
import { ClusterContext } from '../ClusterDetails';

// Icons
import EditIcon from '@app/assets/edit.svg';

const useStyles = makeStyles((theme) => ({
  widget: {
    padding: theme.spacing(2),
    marginBottom: theme.spacing(2)
  }
}));

interface RegionsListProps {
  cluster: ClusterData;
  regions?: RegionListResponse;
}

const getVpcNameWithLink = (value: string, tableMeta: MUIDataTableMeta) => {
  if (value) {
    return (
      <MUILink component={Link} to={`../../network/vpc/vpcs/${String(tableMeta.rowData[0])}`} underline="always">
        {value}
      </MUILink>
    );
  }
  return '-';
};

export const RegionsList: FC<RegionsListProps> = ({ cluster, regions }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const { accountId, projectId } = useParams<App.RouteParams>();
  const context = useContext(ClusterContext);

  const { data: vpcList } = useListSingleTenantVpcsQuery({ accountId, projectId });

  // function which transforms data which will be used for table listing
  const regionsData = cluster?.spec?.cluster_region_info?.map((regionObj) => {
    const vpcName = vpcList?.data?.filter((vpc) => vpc?.info?.id === regionObj?.placement_info?.vpc_id)[0]?.spec?.name;

    return {
      vpcName,
      region: regionObj?.placement_info?.cloud_info?.region,
      vpc_id: regionObj?.placement_info?.vpc_id ?? '-',
      nodes: regionObj?.placement_info?.num_nodes,
      vCPU_per_node: cluster?.spec?.cluster_info?.node_info?.cpu_usage,
      ram_per_node: cluster?.spec?.cluster_info?.node_info?.memory_mb,
      disk_per_node: cluster?.spec?.cluster_info?.node_info?.disk_size_gb
    };
  });

  const getRamText = useCallback(
    (value: number) => {
      return value ? t('units.GB', { value: convertMBtoGB(value, true) }) : '';
    },
    [t]
  );

  // Returns region with flag of the country
  // const getRegionWithFlag = (value: string, regionList: RegionListResponse | undefined) => {
  //   return <RegionWithFlag code={value} regions={regionList?.data} />;
  // };

  // Returns ram in GB
  const getRam = (value: string) => {
    return <span>{getRamText(Number(value ?? 0))}</span>;
  };

  // Returns disk size in GB
  const getDiskSize = useCallback(
    (value: string) => {
      return <span>{t('units.GB', { value })}</span>;
    },
    [t]
  );

  const clusterTier = cluster?.spec.cluster_info.cluster_tier;
  const editingDisabled = false;

  const openEditInfraModal = () => {
    if (context?.dispatch) {
      context.dispatch({ type: OPEN_EDIT_INFRASTRUCTURE_MODAL });
    }
  };

  // Columns array
  const columns = [
    {
      name: 'vpc_id',
      options: {
        filter: true,
        display: false
      }
    },
    // {
    //   name: 'region',
    //   label: t('clusterDetail.settings.regions.region'),
    //   options: {
    //     filter: true,
    //     customBodyRender: (value: string) => getRegionWithFlag(value, regions)
    //   }
    // },
    {
      name: 'nodes',
      label: t('clusterDetail.settings.regions.nodes'),
      options: {
        filter: true
      }
    },
    {
      name: 'vCPU_per_node',
      label: t('clusterDetail.settings.regions.vCPU_per_node'),
      options: {
        filter: true
      }
    },
    {
      name: 'ram_per_node',
      label: t('clusterDetail.settings.regions.ram_per_node'),
      options: {
        filter: true,
        customBodyRender: getRam
      }
    },
    {
      name: 'disk_per_node',
      label: t('clusterDetail.settings.regions.disk_per_node'),
      options: {
        filter: true,
        customBodyRender: getDiskSize
      }
    },
    {
      name: 'vpcName',
      label: t('clusterDetail.settings.regions.vpcName'),
      options: {
        filter: true,
        customBodyRender: getVpcNameWithLink
      }
    }
  ];

  return (
    <Paper className={classes.widget}>
      <Box mb={1.5} display="flex" justifyContent="space-between">
        <Typography variant="h5">{t('clusterDetail.settings.regions.title')}</Typography>
        {cluster?.spec && clusterTier !== ClusterTier.Free && (
          <YBButton variant="ghost" startIcon={<EditIcon />} onClick={openEditInfraModal} disabled={editingDisabled}>
            {t('clusterDetail.settings.editInfrastructure')}
          </YBButton>
        )}
      </Box>
      <Box width="100%" pb={1} pt={1}>
        {regions && (
          <YBTable data={regionsData ?? []} columns={columns} withBorder={false} options={{ pagination: false }} />
        )}
      </Box>
    </Paper>
  );
};
