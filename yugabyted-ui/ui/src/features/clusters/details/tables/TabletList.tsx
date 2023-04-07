import React, { FC, useEffect, useMemo } from 'react';
import { makeStyles, Box, Typography, MenuItem, LinearProgress } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBButton, YBInput, YBLoadingBox, YBSelect, YBTable } from '@app/components';
import { useGetClusterHealthCheckQuery, useGetClusterNodesQuery, useGetClusterTabletsQuery } from '@app/api/src';
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
}));

type DatabaseListProps = {
  /* tabletList: ClusterTable[], */
  selectedTable: string,
  onRefetch: () => void,
}

export const TabletList: FC<DatabaseListProps> = ({ selectedTable, onRefetch }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [node, setNode] = React.useState<string>('');
  const [tabletID, setTabletID] = React.useState<string>('');

  const { data: tablets, isFetching: isFetchingTablets } = useGetClusterTabletsQuery({ query: { refetchOnMount: 'always' }});
  const tableID = useMemo(() => tablets ? Object.values(tablets.data)
    .find(tablet => tablet.table_name === selectedTable)?.table_uuid as string : undefined, [selectedTable, tablets])

  const { data: nodesResponse, isFetching: isFetchingNodes } = useGetClusterNodesQuery({ query: { refetchOnMount: 'always' }});
  const nodeNames = useMemo(() => nodesResponse?.data.map(node => node.name), [nodesResponse])

  const { data: healthCheckData, isFetching: isFetchingHealth } = useGetClusterHealthCheckQuery({ query: { refetchOnMount: 'always' }});
  
  const [tabletList, setTabletList] = React.useState<any[]>([]);
  const [isLoading, setIsLoading] = React.useState<boolean>(false);

  useEffect(() => {
    if (!nodesResponse || !tableID || !healthCheckData) {
      return;
    }

    const populateTablets = async () => {
      let nodes = nodesResponse.data;
      if (node) {
        nodes = nodes.filter(n => n.name === node)
      }
      const nodeHosts = nodes.map(node => node.host);
      if (!nodeHosts) {
        return;
      }

      const getNodeTablets = async (nodeName: string) => {
        try {
          const cpu = await axios.get<string>(`http://${nodeName}:7000/table?id=${tableID}&raw`)
            .then(({ data }) => {
              const parseTable = data.substring(Array.from(data.matchAll(/<table /g))[2].index!);
              const parseRows = Array.from(parseTable.matchAll(/<tr>([\s\S]*?)<\/tr>/g)).slice(1, -1);
              return parseRows.map(row => {
                const tabletID = Array.from(row[1].matchAll(/<th>(.*)<\/th>/g))[0][1]
                const tabletRange = Array.from(row[1].matchAll(/<td>hash_split: \[(.*)\]<\/td>/g))[0][1]
                const tabletLeader = Array.from(row[1].matchAll(/LEADER: <a href=".*">(.*)<\/a>/g))[0][1]
                const tabletFollowers = Array.from(row[1].matchAll(/FOLLOWER: <a href=".*">(.*)<\/a>/g)).map(r => r[1]).join(', ')
                return {
                  id: tabletID,
                  range: tabletRange,
                  leaderNode: tabletLeader,
                  followerNodes: tabletFollowers,
                  status: healthCheckData?.data?.under_replicated_tablets?.includes(tabletID) ? "Under-replicated" : 
                    (healthCheckData?.data?.leaderless_tablets?.includes(tabletID) ? "Unavailable" : ""),
                }
              })
              
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
        tablets?.forEach(tablet => {
          if (!tabletList.find(t => t.id === tablet.id)) {
            tabletList.push(tablet);
          }
        })
      }

      setTabletList(tabletList);
      setIsLoading(false);
    }

    populateTablets();
  }, [nodesResponse, node, tableID, healthCheckData]);

  const data = useMemo(() => {
    let data = tabletList;
    if (tabletID) {
      const searchName = tabletID.toLowerCase();
      data = data.filter(data => data.id.toLowerCase().includes(searchName));
    }
    return data;
  }, [tabletList, tabletID]);


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

  if (isFetchingNodes || isFetchingHealth || isFetchingTablets || isLoading) {
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
        <YBSelect
          value={node}
          onChange={(ev) => setNode(ev.target.value)}
          className={classes.dropdown}
        >
          <MenuItem value={''}>{t('clusterDetail.databases.allNodes')}</MenuItem>
          {nodeNames?.map(nodeName => 
            <MenuItem key={nodeName} value={nodeName}>{nodeName}</MenuItem>
          )}
        </YBSelect>
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
