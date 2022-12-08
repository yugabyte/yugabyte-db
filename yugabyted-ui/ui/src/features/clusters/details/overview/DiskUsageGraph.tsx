import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';
import { roundDecimal } from '@app/helpers';
import { YBProgress } from '@app/components';

const useStyles = makeStyles((theme) => ({
  container: {
    width: '100%'
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase'
  },
  value: {
    paddingTop: theme.spacing(0.57),
  },
  graphText: {
    paddingBottom: theme.spacing(2)
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
  const freeDiskSize = totalDiskSize - usedDiskSize;

  // Get text for disk usage
  const getDiskUsageText = (diskUsageGb: number) => {
    diskUsageGb = roundDecimal(diskUsageGb)
    return t('clusterDetail.overview.usedDiskText', { usedDiskSizeGb: diskUsageGb });
  }

  const getFreeDiskText = (freeDiskGb: number, totalDiskGb: number) => {
    freeDiskGb = roundDecimal(freeDiskGb)
    totalDiskGb = roundDecimal(totalDiskGb)
    return t('clusterDetail.overview.freeDiskText',
      { freeDiskSizeGb: freeDiskGb, totalDiskSizeGb: totalDiskGb });
  }

  // Get text for encryption
  const getUsedPercentageText = (usedPercentage: number) => {
    return t('units.percent', { value: roundDecimal(usedPercentage * 100) })
  }

  // Open edit infra
  // const openEditInfraModal = () => {
  //   if (context?.dispatch) {
  //     context.dispatch({ type: OPEN_EDIT_INFRASTRUCTURE_MODAL });
  //   }
  // };
  const colorObj = {
    green: {
      r: 51,
      g: 158,
      b: 52
    },
    yellow: {
      r: 240,
      g: 204,
      b: 98
    },
    red: {
      r: 233,
      g: 80,
      b: 63
    }
  };

  const calcColor = (value: number) => {
    let color = '#339e34';
    let colorPercentage = 0;
    if (value > 0.3 && value < 0.6) {
      colorPercentage = (10 * value) / 3 - 1;
      color =
        'rgb(' +
        (colorObj.green.r * (1 - colorPercentage) + colorObj.yellow.r * colorPercentage) +
        ', ' +
        (colorObj.green.g * (1 - colorPercentage) + colorObj.yellow.g * colorPercentage) +
        ', ' +
        (colorObj.green.b * (1 - colorPercentage) + colorObj.yellow.b * colorPercentage) +
        ')';
    }
    if (value >= 0.6 && value < 0.8) {
      colorPercentage = 5 * value - 3;
      color =
        'rgb(' +
        (colorObj.yellow.r * (1 - colorPercentage) + colorObj.red.r * colorPercentage) +
        ', ' +
        (colorObj.yellow.g * (1 - colorPercentage) + colorObj.red.g * colorPercentage) +
        ', ' +
        (colorObj.yellow.b * (1 - colorPercentage) + colorObj.red.b * colorPercentage) +
        ')';
    }
    if (value >= 0.8) {
      color = '#E9503F';
    }
    return color;
  };

  return (
    <div className={classes.container}>
      <div>
        <Typography variant="subtitle2" className={classes.label}>
          {t('clusterDetail.overview.diskUsage')}
        </Typography>
        <div className={classes.graphText}>
            <Typography variant="body2" className={classes.value}>
            {getDiskUsageText(usedDiskSize)}
            </Typography>
            <Typography variant="body2" className={classes.value}>
            {getFreeDiskText(freeDiskSize, totalDiskSize)}
            </Typography>
        </div>
        <YBProgress color={calcColor(usedPercentage)} value={usedPercentage * 100}/>
        <Typography variant="body2" className={classes.value}>
          {getUsedPercentageText(usedPercentage)}
        </Typography>
      </div>
    </div>
  );
};
