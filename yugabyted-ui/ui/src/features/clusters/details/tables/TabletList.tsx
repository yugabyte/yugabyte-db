import React, { FC, useEffect, useMemo } from 'react';
import { makeStyles, Box, Typography, MenuItem, LinearProgress } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBButton, YBCheckbox, YBDropdown, YBInput, YBLoadingBox, YBSelect, YBTable } from '@app/components';
import {
    NodeData,
    TabletInfo,
    useGetClusterHealthCheckQuery,
    useGetClusterNodesQuery
} from '@app/api/src';
import SearchIcon from '@app/assets/search.svg';
import RefreshIcon from '@app/assets/refresh.svg';
import { BadgeVariant, YBBadge } from '@app/components/YBBadge/YBBadge';
import axios from 'axios';

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase',
    textAlign: 'start'
  },
  value: {
    paddingTop: theme.spacing(0.36),
    fontWeight: theme.typography.fontWeightMedium as number,
    color: theme.palette.grey[800],
    fontSize: '18px',
    textAlign: 'start'
  },
  arrowComponent: {
    textAlign: 'end',
    '& svg': {
      marginTop: theme.spacing(0.25),
    }
  },
  statContainer: {
    display: 'flex',
    gap: theme.spacing(4),
    marginTop: theme.spacing(3),
    marginBottom: theme.spacing(4),
  },
  dropdown: {
    width: 200,
    marginRight: theme.spacing(1),
    flexGrow: 0
  },
  searchBox: {
    maxWidth: 520,
    flexGrow: 1,
    marginRight: 'auto'
  },
  refreshBtn: {
    marginRight: theme.spacing(1)
  },
  checkbox: {
    padding: theme.spacing(0.5, 0.5, 0.5, 1.5),
  },
  nodeContainer: {
    display: 'flex',
    flexDirection: 'column',
    width: 200,
  }
}));

type DatabaseListProps = {
  /* tabletList: ClusterTable[], */
  selectedTableUuid: string,
  onRefetch: () => void,
}

export const TabletList: FC<DatabaseListProps> = ({ selectedTableUuid, onRefetch }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const tableID = selectedTableUuid;
  console.log(tableID)

  const { data: nodesResponse, isFetching: isFetchingNodes } = useGetClusterNodesQuery({ query: { refetchOnMount: 'always' }});
  const hasReadReplica = !!nodesResponse?.data.find((node) => node.is_read_replica);
  const nodeNames = useMemo(() => nodesResponse?.data.map(node => node.name), [nodesResponse])

  const [nodes, setNodes] = React.useState<string[]>(nodeNames ?? []);
  const [tabletID, setTabletID] = React.useState<string>('');

  React.useEffect(() => {
    if (nodeNames) {
      setNodes(nodeNames);
    }
  }, [nodeNames])

  const handleNodeChange = (newNode: string) => {
    const nodeIndex = nodes.findIndex(n => n === newNode);
    const newNodes = [ ...nodes ];
    if (nodeIndex !== -1) {
      newNodes.splice(nodeIndex, 1);
    } else {
      newNodes.push(newNode);
    }
    setNodes(newNodes);
  }

  const { data: healthCheckData, isFetching: isFetchingHealth } = useGetClusterHealthCheckQuery({ query: { refetchOnMount: 'always' }});
  
  const [tabletList, setTabletList] = React.useState<any[]>([]);
  const [isLoading, setIsLoading] = React.useState<boolean>(false);

  useEffect(() => {
    if (!nodesResponse || !tableID || !healthCheckData) {
      return;
    }
    const populateTablets = async () => {
      let nodeList: NodeData[];
      if (nodes.length > 0) {
        nodeList = nodesResponse.data.filter(n => nodes.find(node => node === n.name))
      } else {
        nodeList = [];
      }
      const nodeHosts = nodeList.map(node => node.host);
      if (!nodeHosts) {
        return;
      }

      // TODO: We probably want to avoid using axios directly and use a React hook like the
      // useGet- queries, but using useQueries instead of useQuery since we need to do a batch
      // of queries, one for each node. But for now this is ok.
      const getNodeTablets = async (nodeName: string) => {
        try {
          const cpu = await axios.get(`/api/table?id=${tableID}&node_address=${nodeName}`)
            .then(({ data }) => {
                return data.tablets.map((tablet: TabletInfo) => {
                  // Determine leader, follower, and read replica nodes
                  let leader = "";
                  let followers: string[] = [];
                  let read_replicas: string[] = [];
                  tablet.raft_config?.forEach(node => {
                      let nodeAddress = node.location
                          ? (new URL(node.location)).hostname
                          : ""
                      switch(node.role) {
                          case "LEADER":
                            leader = nodeAddress;
                            break;
                          case "FOLLOWER":
                            followers.push(nodeAddress)
                            break;
                          case "READ_REPLICA":
                            read_replicas.push(nodeAddress)
                            break;
                          default:
                            console.error("unknown role " + node.role + " for node " + nodeAddress)
                        }
                  });
                  let hashPartition = tablet.partition
                      ? [...tablet.partition?.matchAll(/hash_split: \[(.*)\]/g)][0][1]
                      : ""
                  return {
                      id: tablet.tablet_id,
                      range: hashPartition,
                      leaderNode: leader,
                      followerNodes: followers.join(', '),
                      readReplicaNodes: read_replicas.join(', '),
                      status: healthCheckData?.data?.under_replicated_tablets?.includes(tabletID)
                          ? "Under-replicated"
                          : (healthCheckData?.data?.leaderless_tablets?.includes(tabletID)
                              ? "Unavailable"
                              : ""),
                  }
                });

            })
            .catch(err => { console.error(err); return undefined; })
          return cpu;
        } catch (err) {
          console.error(err);
          return undefined;
        }
      }

      setIsLoading(true);
      
      const tabletList: any[] = [];
      for (let i = 0; i < nodeHosts.length; i++) {
        const node = nodeHosts[i];
        const tablets = await getNodeTablets(node);
        tablets?.forEach((tablet: any) => {
          if (!tabletList.find(t => t.id === tablet.id)) {
            tabletList.push(tablet);
          }
        })
      }

      setTabletList(tabletList);
      setIsLoading(false);
    }

    populateTablets();
  }, [nodesResponse, nodes, tableID, healthCheckData]);

  const data = useMemo(() => {
    let data = tabletList;
    if (tabletID) {
      const searchName = tabletID.toLowerCase();
      data = data.filter(data => data.id.toLowerCase().includes(searchName));
    }
    return data;
  }, [tabletList, tabletID]);


  const columns = useMemo(() => {
    const columns = [
      {
        name: 'id',
        label: t('clusterDetail.databases.tabletID'),
        options: {
          setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
          setCellProps: () => ({ style: { padding: '8px 16px' }}),
        }
      },
      {
        name: 'range',
        label: t('clusterDetail.databases.tabletRange'),
        options: {
          setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
          setCellProps: () => ({ style: { padding: '8px 16px' }}),
        }
      },
      {
        name: 'leaderNode',
        label: t('clusterDetail.databases.leaderNode'),
        options: {
          setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
          setCellProps: () => ({ style: { padding: '8px 16px' }}),
        }
      },
      {
        name: 'followerNodes',
        label: t('clusterDetail.databases.followerNodes'),
        options: {
          setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
          setCellProps: () => ({ style: { padding: '8px 16px' }}),
        }
      },
      {
        name: 'readReplicaNodes',
        label: t('clusterDetail.databases.readReplicaNodes'),
        options: {
          setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
          setCellProps: () => ({ style: { padding: '8px 16px' }}),
        }
      },
      {
        name: 'status',
        label: '',
        options: {
          sort: false,
          hideHeader: true,
          setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
          setCellProps: () => ({ style: { padding: '8px 16px' }}),
          customBodyRender: (status: string) => status && 
            <YBBadge variant={status === "Under-replicated" ? BadgeVariant.Warning : BadgeVariant.Error} 
              text={status === "Under-replicated" ? t('clusterDetail.databases.underReplicated') : 
                t('clusterDetail.databases.unavailable')} />,
        }
      },
    ];

    if (nodesResponse && nodesResponse.data.length < 2) {
      columns.splice(columns.findIndex(col => col.name === "followerNodes"), 1);
    }

    if (!hasReadReplica) {
      columns.splice(columns.findIndex(col => col.name === "readReplicaNodes"), 1);
    }

    return columns;
  }, [nodesResponse, hasReadReplica]);

  if (isFetchingNodes || isFetchingHealth || isLoading) {
    return (
      <Box textAlign="center" mt={2.5}>
        <LinearProgress />
      </Box>
    );
  }

  return (
    <Box>
      <Box className={classes.statContainer}>
        <Box>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.databases.totalTablets')}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {tabletList.length}
          </Typography>
        </Box>
      </Box>
      <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
        {/* <YBSelect
          value={node}
          onChange={(ev) => setNode(ev.target.value)}
          className={classes.dropdown}
        >
          <MenuItem value={''}>{t('clusterDetail.databases.allNodes')}</MenuItem>
          {nodeNames?.map(nodeName => 
            <MenuItem key={nodeName} value={nodeName}>{nodeName}</MenuItem>
          )}
        </YBSelect> */}
        <YBDropdown origin={
          <YBSelect inputProps={{ tabIndex: -1 }} style={{ pointerEvents: "none" }} className={classes.dropdown} 
            value={nodes.join(', ')} >
            <MenuItem value={nodes.join(', ')}>
              {nodes.length === 0 ? t('clusterDetail.databases.none') : 
                (nodes.length <= 2 ? nodes.join(', ') : 
                (nodes.length === nodeNames?.length ? t('clusterDetail.databases.allNodes') : `${nodes.length} nodes`))}
            </MenuItem>
          </YBSelect>}
          position={'bottom'} growDirection={'right'} keepOpenOnSelect>
          <Box className={classes.nodeContainer}>
            {nodeNames?.map(nodeName => 
              <YBCheckbox key={nodeName} label={nodeName} className={classes.checkbox}
                checked={!!nodes.find(n => n === nodeName)}
                onClick={() => handleNodeChange(nodeName)} />
            )}
          </Box>
        </YBDropdown>
        <YBInput
          placeholder={t('clusterDetail.databases.searchTabletID')}
          InputProps={{
            startAdornment: <SearchIcon />
          }}
          className={classes.searchBox}
          onChange={(ev) => setTabletID(ev.target.value)}
          value={tabletID}
        />
        <YBButton variant="ghost" startIcon={<RefreshIcon />} className={classes.refreshBtn} onClick={onRefetch}>
          {t('clusterDetail.performance.actions.refresh')}
        </YBButton>
      </Box>
      {!data.length ?
        <YBLoadingBox>{t('clusterDetail.tables.noTabletsCopy')}</YBLoadingBox>
        :
        <YBTable
          data={data}
          columns={columns}
          touchBorder={false}
        />
      }
    </Box>
  );
};
