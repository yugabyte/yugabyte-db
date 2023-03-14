import React, { FC, useMemo } from 'react';
import { makeStyles, Box, Typography, MenuItem } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { getMemorySizeUnits } from '@app/helpers';
import { YBButton, YBInput, YBLoadingBox, YBSelect, YBTable } from '@app/components';
import { useGetClusterNodesQuery } from '@app/api/src';
import SearchIcon from '@app/assets/search.svg';
import RefreshIcon from '@app/assets/refresh.svg';
import { BadgeVariant, YBBadge } from '@app/components/YBBadge/YBBadge';

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
  onRefetch: () => void,
}

// Dummy data for now
const tabletList = [
  {
    id: "00abf04dc4e14d63ad58f6wqop",
    size: 82000000,
    range: "0x0000, 0x5555",
    leaderNode: "172.12.52.103",
    followerNodes: "172.12.52.102",
    status: "Under-replicated",
  },
  {
    id: "00f577acb945426abaa1bglpaer",
    size: 86000000,
    range: "0x5555, 0xAAAA",
    leaderNode: "172.12.52.103",
    followerNodes: "172.12.52.105, 172.12.52.106",
    status: "",
  },
]

export const TabletList: FC<DatabaseListProps> = ({ onRefetch }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [node, setNode] = React.useState<string>();
  const [tabletID, setTabletID] = React.useState<string>();

  const data = useMemo(() => {
    let data = tabletList;
    if (node !== undefined) {
      data = data.filter(data => data.leaderNode === node);
    }
    if (tabletID !== undefined) {
      const searchName = tabletID.toLowerCase();
      data = data.filter(data => data.id.toLowerCase().includes(searchName));
    }
    return data;
  }, [tabletList, node, tabletID]);

  const { data: nodesResponse } = useGetClusterNodesQuery();
  const nodeList = useMemo(() => nodesResponse?.data.map(node => node.name), [nodesResponse])

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
      name: 'size',
      label: t('clusterDetail.databases.size'),
      options: {
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
        customBodyRender: (value: number) => getMemorySizeUnits(value)
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
        customBodyRender: (status: string) => status && <YBBadge variant={BadgeVariant.Warning} 
          text={t('clusterDetail.databases.underReplicated')} />,
      }
    },
  ];

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
        <Box>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.databases.totalSize')}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {getMemorySizeUnits(tabletList.reduce((prev, curr) => prev + curr.size, 0))}
          </Typography>
        </Box>
      </Box>
      <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
        <YBSelect
          value={node}
          onChange={(ev) => setNode(ev.target.value)}
          className={classes.dropdown}
        >
          <MenuItem value={undefined}>{t('clusterDetail.databases.allNodes')}</MenuItem>
          {nodeList?.map(nodeName => 
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
        <YBLoadingBox>{t('clusterDetail.tables.noTablesCopy')}</YBLoadingBox>
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
