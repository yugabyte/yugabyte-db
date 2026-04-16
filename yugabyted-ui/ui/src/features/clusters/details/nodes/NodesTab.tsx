import React, { FC, useMemo, useState } from 'react';
import { useForm } from 'react-hook-form';
import { Box, MenuItem, Typography, makeStyles, LinearProgress } from '@material-ui/core';
import { TFunction, useTranslation } from 'react-i18next';
import {
    YBTable,
    YBLoadingBox,
    YBStatus,
    STATUS_TYPES,
    YBModal,
    YBCheckboxField,
    YBButton,
    YBSelect
} from '@app/components';
import { getHumanInterval, getMemorySizeUnits, roundDecimal } from '@app/helpers';
import { getHumanVersion } from '@app/features/clusters/ClusterDBVersionBadge';
import { NodeCountWidget } from './NodeCountWidget';
import type { ClassNameMap } from '@material-ui/styles';
import type { MUISortOptions } from 'mui-datatables';
import EditIcon from '@app/assets/edit.svg';
import RefreshIcon from '@app/assets/refresh.svg';
import { StateEnum, StatusEntity, YBSmartStatus } from '@app/components/YBStatus/YBSmartStatus';
import { StringParam, useQueryParams, withDefault } from 'use-query-params';
import { useNodes } from './NodeHooks';
import {
    useGetGflagsQuery,
    GflagsInfo,
    useGetClusterConnectionsQuery,
} from "@app/api/src";
import { YBTextBadge } from '@app/components/YBTextBadge/YBTextBadge';

const useStyles = makeStyles((theme) => ({
    title: {
      marginTop: theme.spacing(3),
      marginBottom: theme.spacing(2.5),
    },
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
    },
    nodeComponent: {
        display: 'flex',
        padding: '12px 32px',
        alignItems: 'center'
    },
    nodeName: {
        color: theme.palette.grey[900],
    },
    nodeHost: {
        color: theme.palette.grey[700]
    },
    nodeBootstrappingIcon: {
        display: 'flex',
        marginLeft: 'auto',
        paddingLeft: '16px',
        alignItems: 'center'
    },
    regionZoneComponent: {
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        gap: '10px',
        padding: '12px 32px',
    },
    selectBox: {
        width: '180px'
    },
    modalCheckboxesComponent: {
        columnCount: 2
    },
    modalCheckboxSection: {
        breakInside: 'avoid',
        marginBottom: '6px'
    },
    modalCheckboxChild: {
        marginLeft: '32px'
    },
    filterContainer: {
        display: 'flex',
        marginBottom: theme.spacing(2),
    },
    filterRow: {
        display: 'flex',
        gap: '10px',
        alignItems: 'center',
    },
    filterRowButtons: {
        marginLeft: 'auto'
    },
    checkbox: {
        padding: '6px 6px 6px 6px'
    },
  }));

// const StatusComponent = (isHealthy: boolean) => (
//   <YBStatus type={isHealthy ? STATUS_TYPES.SUCCESS : STATUS_TYPES.ERROR} tooltip />
// );

const NodeComponent = (classes: ClassNameMap, t: TFunction) => (
    node_data: {
        status: boolean,
        name: string,
        host: string,
        bootstrapping: boolean,
        is_read_replica: boolean,
    },
) => {

    return (
    <Box className={classes.nodeComponent}>
        <YBStatus type={node_data.status ? STATUS_TYPES.SUCCESS : STATUS_TYPES.ERROR} tooltip />
        <Box display="flex" gridGap={10} alignItems="center">
            <Typography variant='body1' className={classes.nodeName}>
                {node_data.name}
            </Typography>
            {/* For now, nodes do not have names, so we use the host as the value of node_data.name.
                Eventually, we should set node_data.name to be the actual name, and then display
                both. */}
            {/* <Typography variant='subtitle1' className={classes.nodeHost}>
                {node_data.host}
            </Typography> */}
            {node_data.is_read_replica &&
                <YBTextBadge>{t('clusterDetail.nodes.readReplica')}</YBTextBadge>
            }
        </Box>
        {node_data.bootstrapping && (
            <div className={classes.nodeBootstrappingIcon}>
                <YBStatus type={STATUS_TYPES.IN_PROGRESS} tooltip />
            </div>
        )}
    </Box>
    );
}

const RegionZoneComponent = (classes: ClassNameMap, t: TFunction) => (
    region_and_zone: {
        region: string,
        zone: string,
        preference: number | undefined
    }
) => {
    return (
    <Box className={classes.regionZoneComponent}>
        <Box>
            <Typography variant='body2' className={classes.nodeName}>
                {region_and_zone.region}
            </Typography>
            <Typography variant='subtitle1' className={classes.nodeHost}>
                {region_and_zone.zone}
            </Typography>
        </Box>
        {region_and_zone.preference !== undefined && region_and_zone.preference !== -1 &&
            <YBTextBadge>
                {t('clusterDetail.nodes.preference', { preference: region_and_zone.preference })}
            </YBTextBadge>
        }
    </Box>
    );
}

const NODE_COLUMNS_LS_KEY = "node-columns";

export const NodesTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [showEditColumns, setShowEditColumns] = useState(false);

  const [queryParams, setQueryParams] = useQueryParams({
    filter: withDefault(StringParam, ''),
    node: withDefault(StringParam, ''),
  });
  const [filter, setFilter] = useState<string | undefined>(queryParams.filter);
  const [nodeFilter, setNodeFilter] = useState<string>(queryParams.node);

  const handleChangeFilter = (
    newFilter: string,
  ) => {
    setFilter(newFilter);
    setQueryParams({
      filter: newFilter || undefined
    });
  };

  const handleChangeNodeFilter = (
    newNodeFilter: string,
  ) => {
    setNodeFilter(newNodeFilter);
    setQueryParams({
      node: newNodeFilter || undefined
    });
  };

  // Get nodes, including master-only nodes
  const { data: nodesResponse, isFetching: fetchingNodes, refetch: refetchNodes } = useNodes(true);
  // address of a live tserver
  const tserverAddress = nodesResponse?.data?.find((node) => node.is_node_up)?.host ?? "";
  const { data: gflagsResponse, isFetching: fetchingGflags, refetch: refetchGflags }
      = useGetGflagsQuery<GflagsInfo>(
        { node_address: tserverAddress },
      );
  const isConnMgrEnabled = useMemo(() => {
    return gflagsResponse?.tserver_flags?.some(flag =>
        flag.name === "enable_ysql_conn_mgr" && flag.value === "true")
  }, [gflagsResponse, nodesResponse]);

  const { data: connectionsResponse, isFetching: fetchingConn, refetch: refetchConn }
      = useGetClusterConnectionsQuery();

  // These define which checkboxes are checked by default in the Edit Columns modal
  const defaultValues : Record<string, boolean> = {
    // categories, for the edit columns modal
      general: true,
      performance: true,
      scalability: true,
      node_status: false,
      memory: false,
      disk_storage: false,
      processes: false,
    // columns, including parent and subcolumns
      node_data: true,
      region_and_zone: true,
      read_write_ops: true,
      read_ops: true,
      write_ops: true,
      ...((isConnMgrEnabled)
          ? {
              active_logical_connections: false,
              active_physical_connections: false,
              active_connections_ycql: false
            }
          : {active_connections: false}),
      number_of_tablets: true,
      peer_tablets: true,
      leader_tablets: true,
      total_tablets: true,
      node_status_column: false,
      uptime_seconds: false,
      time_since_hb_sec: false,
      memory_column: false,
      ram_used: false,
      ram_provisioned: false,
      disk_usage_column: false,
      sst_size: false,
      uncompressed_sst_size: false,
      disk_provisioned: false,
      master_tserver_status: false,
      master_tserver_uptime: false
  };
  const [columns, setColumns] = useState({
    ...defaultValues,
    ...(JSON.parse(localStorage.getItem(NODE_COLUMNS_LS_KEY)!) || {})
  });
  const { control, handleSubmit, reset, setValue, getValues } = useForm({
    mode: 'onChange',
    defaultValues: columns
  });
  const closeQueryOptionsModal = () => setShowEditColumns(false);
  const handleClose = () => {
    reset(columns);
    closeQueryOptionsModal();
  };
  const applyColumnChanges = handleSubmit((formData) => {
    setColumns(formData);
    localStorage.setItem(NODE_COLUMNS_LS_KEY, JSON.stringify(formData));
    closeQueryOptionsModal();
  });

  const nodesData = useMemo(() => {
    if (nodesResponse?.data) {
      return nodesResponse.data.filter((node) => {
        switch (filter) {
            case 'running':
                return node.is_node_up;
            case 'bootstrapping':
                return node.is_bootstrapping;
            case 'down':
                return !node.is_node_up;
            default:
                return true;
        }
      })
      .filter((node) => {
        switch (nodeFilter) {
            case 'primary':
                return !node.is_read_replica;
            case 'readreplica':
                return node.is_read_replica;
            default:
                return true;
        }
      })
      .map((node) => ({
        ...node,
        node_data: {
            status: node.is_node_up,
            name: node.name,
            host: node.host,
            bootstrapping: node.is_bootstrapping,
            is_read_replica: node.is_read_replica
        },
        region_and_zone: {
            region: node.cloud_info.region,
            zone: node.cloud_info.zone,
            preference: node.preference_order,
        },
        status: node.is_node_up,
        name: node.name,
        is_master: node.is_master,
        is_tserver: node.is_tserver,
        cloud: node.cloud_info.cloud,
        region: node.cloud_info.region,
        zone: node.cloud_info.zone,
        software_version: node.software_version ? getHumanVersion(node.software_version) : '-',
        memory_column:
            [node.metrics.ram_used_bytes,
             node.metrics.ram_provisioned_bytes],
        disk_usage_column:
            [node.metrics.total_sst_file_size_bytes,
             node.metrics.uncompressed_sst_file_size_bytes,
             node.metrics.disk_provisioned_bytes],
        read_write_ops:
            [roundDecimal(node.metrics.read_ops_per_sec),
             roundDecimal(node.metrics.write_ops_per_sec),
             ...((isConnMgrEnabled)
                 ? [connectionsResponse?.data?.[node.host]
                        ?.reduce((sum, pool) => sum + pool.active_logical_connections, 0) ?? 0,
                    connectionsResponse?.data?.[node.host]
                        ?.reduce((sum, pool) => sum + pool.active_physical_connections, 0) ?? 0,
                    node.metrics.active_connections.ycql]
                 : [node.metrics.active_connections.ysql + node.metrics.active_connections.ycql])],
        node_status_column:
            [node.is_node_up ? node.metrics.uptime_seconds : -1,
             node.metrics.time_since_hb_sec],
        number_of_tablets:
            [node.metrics.user_tablets_total + node.metrics.system_tablets_total -
               node.metrics.user_tablets_leaders - node.metrics.system_tablets_leaders,
             node.metrics.user_tablets_leaders + node.metrics.system_tablets_leaders,
             node.metrics.user_tablets_total + node.metrics.system_tablets_total],
        processes_column: [
            {
                tserver: node.is_node_up,
                master: node.is_master_up,
                is_tserver: node.is_tserver,
                is_master: node.is_master
            },
            {
                tserver: node.is_node_up && node.metrics
                  ? node.metrics.uptime_seconds
                  : -1,
                master: node.is_master_up && node.metrics
                  ? node.metrics.master_uptime_us
                  : -1,
                is_tserver: node.is_tserver,
                is_master: node.is_master
            }
        ]
      }));
    }
    return [];
  }, [nodesResponse, filter, nodeFilter, connectionsResponse, isConnMgrEnabled]);

  const hasReadReplica = !!nodesResponse?.data.find(node => node.is_read_replica);

  if (fetchingNodes || fetchingGflags || fetchingConn) {
    return (
      <>
        <Box mt={3} mb={2.5}>
          <LinearProgress />
          {/* <div className={classes.loadingCount} /> */}
        </Box>
        <div className={classes.loadingBox} />
      </>
    );
  }

  // The columns of the table. Note: when modifying the columns, remember to update
  // defaultValues, CHEKBOX_PARENTS, CHECKBOXES, and NODES_TABLE_COLUMNS as well.
  // In particular, when adding a subcolumn, make sure the display option for the parent
  // is also updated.
  // This is not very intuitive and prone to errors, specifically typos and forgetting to update
  // things. TODO: make this more robust and simpler. Preferably, put all of these values in
  // one place.
  const NODES_TABLE_COLUMNS = [
    {
      name: 'node_data',
      label: t('clusterDetail.nodes.node'),
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
      options: {
        filter: true,
        customBodyRender: NodeComponent(classes, t),
        display: columns.node_data,
        setCellHeaderProps: () => ({style:{whiteSpace: 'nowrap', padding: '8px 8px 8px 34px' }})
      }
    },
    {
        name: 'region_and_zone',
        label: t('clusterDetail.nodes.regionAndZone'),
        customColumnSort: (order: MUISortOptions['direction']) => {
            return (obj1: { data: any }, obj2: { data: any }) => {
                let val1 = obj1.data.region;
                let val2 = obj2.data.region;
                if (val1 == val2) {
                    val1 = obj1.data.zone;
                    val2 = obj2.data.zone
                }
                let compareResult =
                    val2 < val1
                        ? 1
                        : val2 == val1
                        ? 0
                        : -1;
                return compareResult * (order === 'asc' ? 1 : -1);
            };
          },
        options: {
          filter: true,
          customBodyRender: RegionZoneComponent(classes, t),
          setCellHeaderProps: () => ({style:{whiteSpace: 'nowrap', padding: '8px 32px' }}),
          display: columns.region_and_zone
        }
    },
    {
        name: 'read_write_ops',
        label: t('clusterDetail.nodes.performance'),
        options: {
          filter: true,
          display: columns.read_ops || columns.write_ops || columns.active_connections ||
                   columns.active_logical_connections || columns.active_physical_connections ||
                   columns.active_connections_ycql
        },
        subColumns: [
          {
              name: 'read_ops',
              label: t('clusterDetail.nodes.readOpsPerSec'),
              options: {
                  filter: true,
                  display: columns.read_ops,
                  // We hard code widths for subcolumns for now to ensure the body cells line
                  // up with the header cells. For this to happen automatically, we'll
                  // need to modify YBTable further, which we may or may not want to do.
                  setCellProps: () => ({ style: { width: '150px', padding: '0 32px' } }),
                  setCellHeaderProps: () => ({ style: { width: '150px', padding: '8px 32px' } })
              }
          },
          {
              name: 'write_ops',
              label: t('clusterDetail.nodes.writeOpsPerSec'),
              options: {
                  filter: true,
                  display: columns.write_ops,
                  setCellProps: () => ({ style: { width: '150px', padding: '0 32px' } }),
                  setCellHeaderProps: () => ({ style: { width: '150px', padding: '8px 32px' } })
              }
          },
          ...((isConnMgrEnabled)
              ? [
                    {
                        name: 'active_logical_connections',
                        label: t('clusterDetail.nodes.activeLogicalConnectionsYsql'),
                        options: {
                            filter: true,
                            display: columns.active_logical_connections,
                            setCellProps: () => ({ style: { width: '260px', padding: '0 32px' } }),
                            setCellHeaderProps: () => ({
                                style: { width: '260px', padding: '8px 32px' }
                            })
                        }
                    },
                    {
                        name: 'active_physical_connections',
                        label: t('clusterDetail.nodes.activePhysicalConnectionsYsql'),
                        options: {
                            filter: true,
                            display: columns.active_physical_connections,
                            setCellProps: () => ({ style: { width: '260px', padding: '0 32px' } }),
                            setCellHeaderProps: () => ({
                                style: { width: '260px', padding: '8px 32px' }
                            })
                        }
                    },
                    {
                        name: 'active_connections_ycql',
                        label: t('clusterDetail.nodes.activeConnectionsYcql'),
                        options: {
                            filter: true,
                            display: columns.active_connections_ycql,
                            setCellProps: () => ({ style: { width: '220px', padding: '0 32px' } }),
                            setCellHeaderProps: () => ({
                                style: { width: '220px', padding: '8px 32px' }
                            })
                        }
                    }
                ]
              : [
                    {
                        name: 'active_connections',
                        label: t('clusterDetail.nodes.activeConnections'),
                        options: {
                            filter: true,
                            display: columns.active_connections,
                            setCellProps: () => ({ style: { width: '180px', padding: '0 32px' } }),
                            setCellHeaderProps: () => ({
                                style: { width: '180px', padding: '8px 32px' }
                            })
                        }
                    }
                ]
          ),
        ],
    },
    {
        name: 'number_of_tablets',
        label: t('clusterDetail.nodes.numberOfTablets'),
        options: {
          filter: true,
          display: columns.peer_tablets || columns.leader_tablets || columns.total_tablets
        },
        subColumns: [
          {
              name: 'peer_tablets',
              label: t('clusterDetail.nodes.peer'),
              options: {
                  filter: true,
                  display: columns.peer_tablets,
                  setCellProps: () => ({ style: { width: '100px', padding: '0 32px' } }),
                  setCellHeaderProps: () => ({ style: { width: '100px', padding: '8px 32px' } })
              }
          },
          {
              name: 'leader_tablets',
              label: t('clusterDetail.nodes.leader'),
              options: {
                  filter: true,
                  display: columns.leader_tablets,
                  setCellProps: () => ({ style: { width: '100px', padding: '0 32px' } }),
                  setCellHeaderProps: () => ({ style: { width: '100px', padding: '8px 32px' } })
              }
          },
          {
            name: 'total_tablets',
            label: t('clusterDetail.nodes.total'),
            options: {
                filter: true,
                display: columns.total_tablets,
                setCellProps: () => ({ style: { width: '100px', padding: '0 32px' } }),
                setCellHeaderProps: () => ({ style: { width: '100px', padding: '8px 32px' } })
            }
          },
        ],
    },
    {
        name: 'node_status_column',
        label: t('clusterDetail.nodes.nodeStatus'),
        options: {
            filter: true,
            display: columns.uptime_seconds || columns.time_since_hb_sec
        },
        subColumns: [
            {
                name: 'uptime_seconds',
                label: t('clusterDetail.nodes.nodeUptime'),
                options: {
                    filter: true,
                    display: columns.uptime_seconds,
                    setCellProps: () => ({ style: { width: '120px', padding: '0 32px' } }),
                    setCellHeaderProps: () => ({ style: { width: '120px', padding: '8px 32px' } })
                }
            },
            {
                name: 'time_since_hb_sec',
                label: t('clusterDetail.nodes.timeSinceHeartbeat'),
                options: {
                    filter: true,
                    display: columns.time_since_hb_sec,
                    setCellProps: () => ({ style: { width: '200px', padding: '0 32px' } }),
                    setCellHeaderProps: () => ({ style: { width: '200px', padding: '8px 32px' } })
                }
            }
        ],
        customColumnBodyRender: (value: number, index: number) => {
            if (index == 0) {
                return <>
                    {value >= 0
                        ? getHumanInterval(new Date(0).toString(),
                            new Date(value * 1000).toString())
                        : '-'}
                </>
            } else if (index == 1) {
                return <>{`${Math.round(value * 1000)} ms`}</>
            }
            return <></>;
        },
    },
    {
        name: 'memory_column',
        label: t('clusterDetail.nodes.memory'),
        options: {
            filter: true,
            display: columns.ram_used || columns.ram_provisioned
        },
        subColumns: [
            {
                name: 'ram_used',
                label: t('clusterDetail.nodes.used'),
                options: {
                    filter: true,
                    display: columns.ram_used,
                    setCellProps: () => ({ style: { width: '120px', padding: '0 32px' } }),
                    setCellHeaderProps: () => ({ style: { width: '120px', padding: '8px 32px' } })
                }
            },
            {
                name: 'ram_provisioned',
                label: t('clusterDetail.nodes.provisioned'),
                options: {
                    filter: true,
                    display: columns.ram_provisioned,
                    setCellProps: () => ({ style: { width: '140px', padding: '0 32px' } }),
                    setCellHeaderProps: () => ({ style: { width: '140px', padding: '8px 32px' } })
                }
            }
        ],
        customColumnBodyRender: (value: number) => {
            return <>{`${getMemorySizeUnits(value)}`}</>;
        },
    },
    {
        name: 'disk_usage_column',
        label: t('clusterDetail.nodes.diskStorage'),
        options: {
            filter: true,
            display: columns.sst_size || columns.uncompressed_sst_size || columns.disk_provisioned
        },
        subColumns: [
            {
                name: 'sst_size',
                label: t('clusterDetail.nodes.sstSize'),
                options: {
                    filter: true,
                    display: columns.sst_size,
                    setCellProps: () => ({ style: { width: '120px', padding: '0 32px' } }),
                    setCellHeaderProps: () => ({ style: { width: '120px', padding: '8px 32px' } })
                }
            },
            {
                name: 'uncompressed_sst_size',
                label: t('clusterDetail.nodes.uncompressedSstSize'),
                options: {
                    filter: true,
                    display: columns.uncompressed_sst_size,
                    setCellProps: () => ({ style: { width: '200px', padding: '0 32px' } }),
                    setCellHeaderProps: () => ({ style: { width: '200px', padding: '8px 32px' } })
                }
            },
            {
                name: 'disk_provisioned',
                label: t('clusterDetail.nodes.diskProvisioned'),
                options: {
                    filter: true,
                    display: columns.disk_provisioned,
                    setCellProps: () => ({ style: { width: '160px', padding: '0 32px' } }),
                    setCellHeaderProps: () => ({ style: { width: '160px', padding: '8px 32px' } })
                }
            }
        ],
        customColumnBodyRender: (value: number) => {
            return <>{`${getMemorySizeUnits(value)}`}</>;
        },
    },
    {
        name: 'processes_column',
        label: t('clusterDetail.nodes.processes'),
        customColumnSort: (order: MUISortOptions['direction']) => {
            return (obj1: any, obj2: any) => {
                let val1 = obj1.tserver;
                let val2 = obj2.tserver;
                if (val1 == val2) {
                    val1 = obj1.master;
                    val2 = obj2.master;
                }
                let compareResult =
                    val2 < val1
                        ? 1
                        : val2 == val1
                        ? 0
                        : -1;
                return compareResult * (order === 'asc' ? 1 : -1);
            };
          },
        options: {
          filter: true,
          // setCellProps: () => ({ style: { width: '200px' } }),
          display: columns.master_tserver_status || columns.master_tserver_uptime
        },
        subColumns: [
            {
                name: 'master_tserver_status',
                label: t('clusterDetail.nodes.status'),
                options: {
                    filter: true,
                    display: columns.master_tserver_status,
                    setCellProps: () => ({ style: { width: '140px', padding: '0 32px 0' } }),
                    setCellHeaderProps: () => ({ style: { width: '140px', padding: '8px 32px' } })
                }
            },
            {
                name: 'master_tserver_uptime',
                label: t('clusterDetail.nodes.uptime'),
                options: {
                    filter: true,
                    display: columns.master_tserver_uptime,
                    setCellProps: () => ({ style: { width: '100px', padding: '0 32px' } }),
                    setCellHeaderProps: () => ({ style: { width: '100px', padding: '8px 32px' } })
                }
            }
        ],
        customColumnBodyRender: (value: any, index: number) => {
            if (index == 0) {
                return (
                    <>
                        {value.is_tserver && <div style={{ 'margin': '6px 0' }}>
                            <YBSmartStatus
                                status={value.tserver ? StateEnum.Succeeded : StateEnum.Failed}
                                entity={StatusEntity.Tserver}
                            />
                        </div>}
                        {value.is_master && <div style={{ 'margin': '6px 0' }}>
                            <YBSmartStatus
                                status={value.master ? StateEnum.Succeeded : StateEnum.Failed}
                                entity={StatusEntity.Master}
                            />
                        </div>}
                    </>
                );
            } else if (index == 1) {
                return (
                    <>
                        {value.is_tserver && <div style={{ 'margin': '8px 0' }}>
                            {value.tserver >= 0
                                ? getHumanInterval(new Date(0).toString(),
                                    new Date(value.tserver * 1000).toString())
                                : '-'}
                        </div>}
                        {value.is_master && <div style={{ 'margin': '12px 0 8px 0' }}>
                            {value.master >= 0
                                ? getHumanInterval(new Date(0).toString(),
                                    new Date(value.master / 1000).toString())
                                : '-'}
                        </div>}
                    </>
                );
            }
            return <></>;
        },
    }
  ];

  // Parent categories for all columns and subcolumns (not parent columns)
  // Used to allow category to be unchecked when it detects all children are unchecked
  const CHECKBOX_PARENTS = {
    node_data: 'general',
    region_and_zone: 'general',
    read_ops: 'performance',
    write_ops: 'performance',
    ...((isConnMgrEnabled)
        ? {
            active_logical_connections: 'performance',
            active_physical_connections: 'performance',
            active_connections_ycql: 'performance'
          }
        : {active_connections: 'performance'}),
    ram_used: 'memory',
    ram_provisioned: 'memory',
    peer_tablets: 'scalability',
    leader_tablets: 'scalability',
    total_tablets: 'scalability',
    uptime_seconds: 'node_status',
    time_since_hb_sec: 'node_status',
    sst_size: 'disk_storage',
    uncompressed_sst_size: 'disk_storage',
    disk_provisioned: 'disk_storage',
    master_tserver_status: 'processes',
    master_tserver_uptime: 'processes'
  };

  // This defines the checkboxes in the Edit Columns modal.
  const CHECKBOXES = {
    general: {
        label: t('clusterDetail.nodes.general'),
        columns: [
            {
                name: 'node_data',
                label: t('clusterDetail.nodes.nodeAndIpAddress')
            },
            {
                name: 'region_and_zone',
                label: t('clusterDetail.nodes.regionAndZone')
            },
        ]
    },
    performance: {
        label: t('clusterDetail.nodes.performance'),
        columns: [
            {
                name: 'read_ops',
                label: t('clusterDetail.nodes.readOpsPerSec')
            },
            {
                name: 'write_ops',
                label: t('clusterDetail.nodes.writeOpsPerSec')
            },
            ...((isConnMgrEnabled)
                ? [
                    {
                        name: 'active_logical_connections',
                        label: t('clusterDetail.nodes.activeLogicalConnectionsYsql')
                    },
                    {
                        name: 'active_physical_connections',
                        label: t('clusterDetail.nodes.activePhysicalConnectionsYsql')
                    },
                    {
                        name: 'active_connections_ycql',
                        label: t('clusterDetail.nodes.activeConnectionsYcql')
                    }
                  ]
                : [
                    {
                        name: 'active_connections',
                        label: t('clusterDetail.nodes.activeConnections')
                    }
                  ]
            ),
        ]
    },
    scalability: {
        label: t('clusterDetail.nodes.scalability'),
        columns: [
            {
                name: 'peer_tablets',
                label: t('clusterDetail.nodes.peerTablets')
            },
            {
                name: 'leader_tablets',
                label: t('clusterDetail.nodes.leaderTablets')
            },
            {
                name: 'total_tablets',
                label: t('clusterDetail.nodes.totalTablets')
            }
        ]
    },
    node_status: {
        label: t('clusterDetail.nodes.nodeStatus'),
        columns: [
            {
                name: 'uptime_seconds',
                label: t('clusterDetail.nodes.nodeUptime')
            },
            {
                name: 'time_since_hb_sec',
                label: t('clusterDetail.nodes.timeSinceHeartbeat')
            }
        ]
    },
    memory: {
        label: t('clusterDetail.nodes.memory'),
        columns: [
            {
                name: 'ram_used',
                label: t('clusterDetail.nodes.memoryUsed')
            },
            {
                name: 'ram_provisioned',
                label: t('clusterDetail.nodes.memoryProvisioned')
            }
        ]
    },
    disk_storage: {
        label: t('clusterDetail.nodes.diskStorage'),
        columns: [
            {
                name: 'sst_size',
                label: t('clusterDetail.nodes.sstSize')
            },
            {
                name: 'uncompressed_sst_size',
                label: t('clusterDetail.nodes.uncompressedSstSize')
            },
            {
                name: 'disk_provisioned',
                label: t('clusterDetail.nodes.storageProvisioned')
            }
        ]
    },
    processes: {
        label: t('clusterDetail.nodes.processes'),
        columns: [
            {
                name: 'master_tserver_status',
                label: t('clusterDetail.nodes.masterAndTserverStatus')
            },
            {
                name: 'master_tserver_uptime',
                label: t('clusterDetail.nodes.masterAndTserverUptime')
            }
        ]
    }
  };

  const MODAL_CHECKBOXES_COMPONENT =
  <div className={classes.modalCheckboxesComponent}>
    {Object.entries(CHECKBOXES).map(([key, value]) => {
        return (
            <div className={classes.modalCheckboxSection}>
                <YBCheckboxField
                name={key}
                label={
                    <Typography variant='body1' className={classes.nodeName}>{
                        value.label}
                    </Typography>
                }
                control={control}
                className={classes.checkbox}
                onChange={(e) => {
                    // Selecting a category checkbox selects all columns under it
                    setValue(key, e.target.checked);
                    for (const column of value.columns) {
                        setValue(column.name, e.target.checked)
                    }
                    setValue("node_data", true)
                }}
                />
                <div className={classes.modalCheckboxChild}>
                    {value.columns.map(column => {
                        return (
                            <YBCheckboxField
                            name={column.name}
                            label={column.label}
                            control={control}
                            disabled={column.name == "node_data"}
                            className={classes.checkbox}
                            onChange={(e) => {
                                // Selecting at least one column will cause parent checkbox to be
                                // selected.
                                // If all boxes of a category are deselected, the parent is also
                                // deselected.
                                setValue(column.name, e.target.checked);
                                let parentName =
                                    CHECKBOX_PARENTS[column.name as keyof typeof CHECKBOX_PARENTS];
                                let orAllChildren =
                                    CHECKBOXES[parentName as keyof typeof CHECKBOXES].columns
                                    .reduce((accumulator, current) =>
                                        accumulator ||
                                        getValues(current.name) && (current.name != "node_data"),
                                        false
                                    );
                                setValue(parentName, e.target.checked || orAllChildren);
                            }}
                            />
                        );
                    })}
                </div>
            </div>
        );
    })}
  </div>


  return (
    <>
      <Box mt={3} mb={2}>
        <NodeCountWidget nodes={nodesResponse} />
      </Box>
      <Box className={classes.filterContainer}>
        <Box className={classes.filterRow}>
            <YBSelect
                className={classes.selectBox}
                value={filter}
                onChange={(e) => {
                    handleChangeFilter(
                        (e.target as HTMLInputElement).value
                    );
                }}
            >
                    <MenuItem value={''}>
                    {t('clusterDetail.nodes.allNodeStatuses')}
                    </MenuItem>
                    <MenuItem value={'running'}>
                    <YBStatus type={STATUS_TYPES.SUCCESS}/>
                    {t('clusterDetail.nodes.running')}</MenuItem>
                    <MenuItem value={'bootstrapping'}>
                    <YBStatus type={STATUS_TYPES.IN_PROGRESS}/>
                    {t('clusterDetail.nodes.bootstrapping')}
                    </MenuItem>
                    <MenuItem value={'down'}>
                    <YBStatus type={STATUS_TYPES.ERROR}/>
                    {t('clusterDetail.nodes.down')}
                    </MenuItem>
            </YBSelect>
            {hasReadReplica &&
                <YBSelect
                    className={classes.selectBox}
                    value={nodeFilter}
                    onChange={(e) => {
                        handleChangeNodeFilter(
                            (e.target as HTMLInputElement).value
                        );
                    }}
                >
                        <MenuItem value={''}>
                            {t('clusterDetail.nodes.allNodes')}
                        </MenuItem>
                        <MenuItem value={'primary'}>
                            {t('clusterDetail.nodes.primaryNodes')}
                        </MenuItem>
                        <MenuItem value={'readreplica'}>
                            {t('clusterDetail.nodes.readReplicaNodes')}
                        </MenuItem>
                </YBSelect>
            }
        </Box>
        <Box className={classes.filterRowButtons}>
            <YBButton
                variant="ghost"
                startIcon={<RefreshIcon />}
                onClick={() =>  {
                    refetchNodes();
                    refetchGflags();
                    refetchConn();
                }}
                >
                {t('clusterDetail.nodes.refresh')}
            </YBButton>
            <YBButton
                variant="ghost"
                startIcon={<EditIcon />}
                onClick={() => setShowEditColumns(true)}
            >
                {t('clusterDetail.nodes.editColumns')}
            </YBButton>
        </Box>
      </Box>
      <YBModal
        open={showEditColumns}
        title={t('clusterDetail.nodes.editColumns')}
        onClose={handleClose}
        onSubmit={applyColumnChanges}
        enableBackdropDismiss
        titleSeparator
        submitLabel={t('common.apply')}
        cancelLabel={t('common.cancel')}
        isSidePanel
      >
        <Box mt={1} mb={1}>
          <Typography variant='body2'>
            {'Display the following columns'}
          </Typography>
        </Box>
        {MODAL_CHECKBOXES_COMPONENT}
      </YBModal>
      { nodesData.length ?
        <Box pb={4}>
            <YBTable
                data={nodesData}
                columns={NODES_TABLE_COLUMNS}
                options={{ pagination: false }}
                touchBorder
                cellBorder
                noCellBottomBorder
                alternateRowShading
            />
        </Box>
        : <YBLoadingBox>{t('clusterDetail.nodes.noNodesCopy')}</YBLoadingBox>
      }
    </>
  )
};
