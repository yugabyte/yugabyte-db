import React, { FC } from 'react';
import { Typography, makeStyles, Box } from '@material-ui/core';
import CheckIcon from '@app/assets/check.svg';
import { useTranslation } from 'react-i18next';
import LoadingIcon from '@app/assets/Default-Loading-Circles.svg';
// import { CLUSTER_STATE } from '@app/features/clusters/list/ClusterCard';

const useStyles = makeStyles((theme) => ({
  container: {
    position: 'relative',
    padding: theme.spacing(2, 6),
    margin: theme.spacing(0, 2),
    display: 'flex',
    justifyContent: 'space-between'
  },
  line: {
    height: '1px',
    width: '100%',
    backgroundColor: theme.palette.grey[200],
    position: 'absolute',
    top: theme.spacing(6.75),
    left: 0
  },
  step: {
    position: 'relative',
    display: 'flex',
    flexDirection: 'column',
    textAlign: 'center',
    width: '240px'
  },
  icon: {
    height: theme.spacing(4),
    width: theme.spacing(4),
    borderRadius: '50%',
    backgroundColor: theme.palette.grey[300],
    color: theme.palette.grey[900],
    margin: theme.spacing(1, 'auto'),
    padding: theme.spacing(0.75, 0),
    fontSize: '15px',
    lineHeight: '18px',
    fontWeight: 700
  },
  iconWrapper: {
    height: theme.spacing(4),
    width: theme.spacing(4),
    margin: theme.spacing(1, 'auto')
  },
  activeIcon: {
    backgroundColor: theme.palette.primary[600],
    color: theme.palette.common.white,
    viewBox: '3px 3px 18px 18px'
  },
  completedIcon: {
    borderRadius: '50%',
    backgroundColor: theme.palette.success[500],
    color: theme.palette.common.white
  },
  description: {
    color: theme.palette.grey[600]
  },
  loadingIcon: {
    width: theme.spacing(4),
    height: theme.spacing(4)
  }
}));

interface StepperProps {
}

export const ProgressStepper: FC<StepperProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  let currentStep = 1;

  // if (state === CLUSTER_STATE.Bootstrap) {
  //   currentStep = 1;
  // } else if (state === CLUSTER_STATE.Provision) {
  //   currentStep = 2;
  // } else if (state === CLUSTER_STATE.Configure || state === CLUSTER_STATE.CreateLB) {
  //   currentStep = 3;
  // }

  const completedCheckIcon = (
    <div className={classes.iconWrapper}>
      <CheckIcon width="32" height="32" className={classes.completedIcon} />
    </div>
  );

  const getStepNumberIcon = (step: number) => {
    if (currentStep === step) {
      return (
        <Box mx="auto" my={1} bgcolor="white" px={0.5}>
          <LoadingIcon className={classes.loadingIcon} />
        </Box>
      );
    }
    return <span className={classes.icon}>{step.toString()}</span>;
  };

  return (
    <div className={classes.container}>
      <div className={classes.line}></div>
      <div className={classes.step}>
        <Typography variant="button">{t('clusterDetail.voyager.export')}</Typography>
        {currentStep > 1 ? completedCheckIcon : getStepNumberIcon(1)}
        <Typography variant="subtitle1" className={classes.description}>
          {t('clusterDetail.voyager.exportDesc')}
        </Typography>
      </div>
      <div className={classes.step}>
        <Typography variant="button">{t('clusterDetail.voyager.analyze')}</Typography>
        {currentStep > 2 ? completedCheckIcon : getStepNumberIcon(2)}
        <Typography variant="subtitle1" className={classes.description}>
          {t('clusterDetail.voyager.analyzeDesc')}
        </Typography>
      </div>
      <div className={classes.step}>
        <Typography variant="button">{t('clusterDetail.voyager.import')}</Typography>
        {currentStep > 3 ? completedCheckIcon : getStepNumberIcon(3)}
        <Typography variant="subtitle1" className={classes.description}>
          {t('clusterDetail.voyager.importDesc')}
        </Typography>
      </div>
    </div>
  );
};
