import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Grid, makeStyles, Paper, Typography } from '@material-ui/core';

// Local imports
import type { ClusterTable } from '@app/api/src';

const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    padding: theme.spacing(2),
    border: `1px solid ${theme.palette.grey[200]}`
  },
  container: {
    justifyContent: 'start',
    marginTop: theme.spacing(0.75)
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase'
  },
  value: {
    paddingTop: theme.spacing(0.57)
  },
  item: {
    marginRight: theme.spacing(8)
  }
}));

interface TableWidgetProps {
  ysqlTableData: ClusterTable[];
  ycqlTableData: ClusterTable[];
}

export const TablesWidget: FC<TableWidgetProps> = ({ ysqlTableData, ycqlTableData }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const numYsqlTables = ysqlTableData?.length;
  const numYcqlTables = ycqlTableData?.length;
  return (
    <Paper className={classes.clusterInfo}>
      <Typography variant="h5">{t('clusterDetail.tables.tables')}</Typography>
      <Grid container className={classes.container}>
        <div className={classes.item}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.tables.ysql')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {numYsqlTables}
          </Typography>
        </div>
        <div className={classes.item}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.tables.ycql')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {numYcqlTables}
          </Typography>
        </div>
      </Grid>
    </Paper>
  );
};
