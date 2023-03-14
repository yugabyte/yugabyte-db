import React, { FC, useMemo } from 'react';
import { makeStyles, Box, Typography, MenuItem } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { getMemorySizeUnits } from '@app/helpers';
import ArrowRightIcon from '@app/assets/caret-right-circle.svg';
import { YBButton, YBInput, YBLoadingBox, YBSelect, YBTable } from '@app/components';
import type { ClusterTable } from '@app/api/src';
import SearchIcon from '@app/assets/search.svg';
import RefreshIcon from '@app/assets/refresh.svg';

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
  leftBorder: {
    paddingStart: `${theme.spacing(2)}px`,
    borderWidth: '0 0 0 1px',
    borderStyle: 'solid',
    borderColor: theme.palette.grey[300],
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
  tableList: ClusterTable[],
  onRefetch: () => void,
  onSelect: (db: string) => void,
}

const ArrowComponent = (classes: ReturnType<typeof useStyles>) => () => {
  return (
    <Box className={classes.arrowComponent}>
      <ArrowRightIcon />
    </Box>
  );
}

export const TableList: FC<DatabaseListProps> = ({ tableList, onSelect, onRefetch }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [tabletStatus, setTabletStatus] = React.useState<string>();
  const [tableName, setTableName] = React.useState<string>();

  const tabletStatusList = useMemo(() => [
    t('clusterDetail.databases.healthyReplication'),
    t('clusterDetail.databases.underReplicated'),
    t('clusterDetail.databases.unavailable'),
  ], [t]);

  const data = useMemo(() => {
    let data = tableList;
    if (tabletStatus !== undefined) {
      data = data.filter(data => (data as any).status === tabletStatus);
    }
    if (tableName !== undefined) {
      const searchName = tableName.toLowerCase();
      data = data.filter(data => data.name.toLowerCase().includes(searchName));
    }
    return data;
  }, [tableList, tabletStatus, tableName])

  const columns = [
    {
      name: 'name',
      label: t('clusterDetail.databases.table'),
      options: {
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
      }
    },
    {
      name: 'keyspace',
      label: t('clusterDetail.databases.partition'),
      options: {
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
      }
    },
    {
      name: 'size_bytes',
      label: t('clusterDetail.databases.size'),
      options: {
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
        customBodyRender: (value: number) => getMemorySizeUnits(value)
      }
    },
    /* {
      name: '',
      label: '',
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px', display: 'flex', justifyContent: 'center' }}),
        customBodyRender: () => <YBBadge variant={BadgeVariant.Warning} 
          text={t('clusterDetail.databases.underReplicated')} />,
      }
    }, */
    {
      name: '',
      label: '',
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
        customBodyRender: ArrowComponent(classes),
      }
    }
  ];

  return (
    <Box>
      <Box className={classes.statContainer}>
        <Box>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.databases.totalTables')}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {tableList.length}
          </Typography>
        </Box>
        <Box>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.databases.totalSize')}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {getMemorySizeUnits(tableList.reduce((prev, curr) => prev + curr.size_bytes, 0))}
          </Typography>
        </Box>
        <Box className={classes.leftBorder}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.databases.underReplicatedTables')}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            0 {/* TODO: Replace with actual data */}
          </Typography>
        </Box>
      </Box>
      <Typography variant="subtitle2" className={classes.label} style={{ marginBottom: 0 }}>
        {t('clusterDetail.databases.tabletStatus')}
      </Typography>
      <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
        <YBSelect
          value={tabletStatus}
          onChange={(ev) => setTabletStatus(ev.target.value)}
          className={classes.dropdown}
        >
          <MenuItem value={undefined}>{t('clusterDetail.databases.allStatuses')}</MenuItem>
          {tabletStatusList.map(tableStatus => 
            <MenuItem key={tableStatus} value={tableStatus}>{tableStatus}</MenuItem>
          )}
          
        </YBSelect>
        <YBInput
          placeholder={t('clusterDetail.databases.searchTableName')}
          InputProps={{
            startAdornment: <SearchIcon />
          }}
          className={classes.searchBox}
          onChange={(ev) => setTableName(ev.target.value)}
          value={tableName}
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
          options={{
            pagination: true,
            rowHover: true, 
            onRowClick: (_, { dataIndex }) => onSelect(data[dataIndex].name) }}
          touchBorder={false}
        />
      }
    </Box>
  );
};
