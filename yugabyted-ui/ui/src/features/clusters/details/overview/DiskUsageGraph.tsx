import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Grid, Link, makeStyles, Typography } from '@material-ui/core';
import { roundDecimal } from '@app/helpers';
import { YBProgress } from '@app/components';
import clsx from 'clsx';
import { ChevronRight } from '@material-ui/icons';

const useStyles = makeStyles((theme) => ({
  container: {
    width: '100%'
  },
  mainContent: {
    marginTop: theme.spacing(6),
    marginBottom: theme.spacing(9),
  },
  title: {
    color: theme.palette.grey[900],
    fontWeight: theme.typography.fontWeightRegular as number,
    flexGrow: 1,
  },
  arrow: {
    color: theme.palette.grey[600],
    marginTop: theme.spacing(0.5)
  },
  label: {
    color: theme.palette.grey[700],
    fontWeight: theme.typography.fontWeightRegular as number,
    marginTop: theme.spacing(0.2),
    textTransform: 'uppercase',
  },
  graphLabel: {
    marginBottom: theme.spacing(1),
  },
  flex: {
    display: "flex",
    alignItems: "bottom",
    gap: theme.spacing(0.5),
  },
  marginRight: {
    marginRight: theme.spacing(1),
  },
  largeMarginRight: {
    marginRight: theme.spacing(4.15),
  },
  marginTop: {
    marginTop: theme.spacing(0.1),
  },
  marginBottom: {
    marginBottom: theme.spacing(1),
  }
}));

interface DiskUsageGraphProps {
  totalDiskSize: number;
  usedDiskSize: number;
}

export const DiskUsageGraph: FC<DiskUsageGraphProps> = ({ totalDiskSize, usedDiskSize }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  // const context = useContext(ClusterContext);

  var usedPercentage = usedDiskSize / totalDiskSize;
  if (isNaN(usedPercentage)) {
    usedPercentage = 0;
  }

  // const freeDiskSize = totalDiskSize - usedDiskSize;

  const getUsedPercentageText = (usedPercentage: number) => {
    return t('units.percent', { value: roundDecimal(usedPercentage * 100) })
  }

  return (
    <div className={classes.container}>
      <Box display="flex" alignItems="center">
        <Typography variant="body2" className={classes.title}>{t('clusterDetail.overview.diskUsage')}</Typography>
        <Link>
          <ChevronRight className={classes.arrow} />
        </Link>
      </Box>
      <div className={classes.mainContent}>
        <Grid container className={clsx(classes.flex, classes.graphLabel)}>
          <Grid item>
            <Typography variant="h5">
              {getUsedPercentageText(usedPercentage)}
            </Typography>
          </Grid>
          <Grid item>
          <Typography variant="body2" className={classes.label}>
            {t('clusterDetail.overview.used')}
          </Typography>
          </Grid>
        </Grid>
        <YBProgress value={usedPercentage * 100}/>
      </div>
      <div className={clsx(classes.flex, classes.marginBottom)}>
        <Typography variant="body2" className={clsx(classes.label, classes.largeMarginRight)}>
          {t('clusterDetail.overview.usage')}
        </Typography>
        <Typography variant="h5">
          {roundDecimal(usedDiskSize)}
        </Typography>
        <Typography variant="body2" className={classes.marginTop}>GB</Typography>
      </div>
      <div className={classes.flex}>
        <Typography variant="body2" className={clsx(classes.label, classes.marginRight)}>
          {t('clusterDetail.overview.available')}
        </Typography>
        <Typography variant="body2">
          {roundDecimal(totalDiskSize)} GB
        </Typography>
      </div>
    </div>
  );
};
