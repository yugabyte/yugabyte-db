import React, { FC } from 'react';
import { makeStyles, Box, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { getMemorySizeUnits } from '@app/helpers';
import ArrowRightIcon from '@app/assets/caret-right-circle.svg';
import { YBButton, YBLoadingBox, YBTable } from '@app/components';
import type { DatabaseListType } from './TablesTab';
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
    marginTop: theme.spacing(3),
  },
  refreshBtn: {
    marginRight: theme.spacing(1)
  },
}));

type DatabaseListProps = {
  dbList: DatabaseListType,
  onRefetch: () => void,
  onSelect: (db: string) => void,
  isYcql: boolean,
}

const ArrowComponent = (classes: ReturnType<typeof useStyles>) => () => {
  return (
    <Box className={classes.arrowComponent}>
      <ArrowRightIcon />
    </Box>
  );
}

export const DatabaseList: FC<DatabaseListProps> = ({ dbList: data, onSelect, isYcql, onRefetch }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const columns = [
    {
      name: 'name',
      label: 
        isYcql
          ? t('clusterDetail.databases.keyspace')
          : t('clusterDetail.databases.database'),
      options: {
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } }),
        setCellProps: () => ({ style: { padding: '8px 16px' }}),
      }
    },
    {
      name: 'tableCount',
      label: t('clusterDetail.databases.table'),
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
        <Typography variant="subtitle2" className={classes.label}>
          {isYcql 
            ? t('clusterDetail.databases.ycqlKeyspaces')
            : t('clusterDetail.databases.ysqlDatabases')
          }
        </Typography>
        <Typography variant="body2" className={classes.value}>
          {data.length}
        </Typography>
      </Box>
      <Box display="flex" alignItems="center" justifyContent="end" mb={2}>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} className={classes.refreshBtn} onClick={onRefetch}>
          {t('clusterDetail.performance.actions.refresh')}
        </YBButton>
      </Box>
      {!data.length ?
        <YBLoadingBox>{t('clusterDetail.databases.noDatabases')}</YBLoadingBox>
        :
        <YBTable
          data={data}
          columns={columns}
          options={{
            pagination: false,
            rowHover: true, 
            onRowClick: (_, { dataIndex }) => onSelect(data[dataIndex].name) }}
          touchBorder={false}
        />
      }
    </Box>
  );
};
