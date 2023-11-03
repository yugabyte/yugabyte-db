/*
 * Created on Tue Jun 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useContext } from 'react';
import { Grid, Typography, makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { RestoreContextMethods, RestoreFormContext } from '../RestoreContext';
import { ybFormatDate } from '../../../../../redesign/helpers/DateUtils';

type BackupInfoBannerProps = {};

const useStyles = makeStyles((theme) => ({
  root: {
    padding: theme.spacing(2),
    background: '#F7F7F7',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1),
    fontWeight: 400,
    fontSize: theme.spacing(1.5),
    height: '67px'
  },
  header: {
    fontSize: theme.spacing(1.5),
    marginBottom: theme.spacing(0.5)
  },
  textCenter: {
    textAlign: 'center'
  },
  textRight: {
    textAlign: 'right'
  }
}));

const BackupInfoBanner: FC<BackupInfoBannerProps> = () => {
  const restoreFormContext: RestoreContextMethods = (useContext(
    RestoreFormContext
  ) as unknown) as RestoreContextMethods;

  const { backupDetails } = restoreFormContext[0];

  const classes = useStyles();

  const { t } = useTranslation();

  return (
    <Grid container item className={classes.root}>
      <Grid item xs={4}>
        <Typography variant="body1" className={classes.header}>
          {t('newRestoreModal.backupInfoBanner.backupSource')}
        </Typography>
        <div>{backupDetails?.universeName}</div>
      </Grid>
      <Grid item xs={4} className={classes.textCenter}>
        <Typography variant="body1" className={classes.header}>
          {t('newRestoreModal.backupInfoBanner.numberOfKeyspace')}
        </Typography>
        <div>{backupDetails?.commonBackupInfo.responseList.length}</div>
      </Grid>
      <Grid item xs={4} className={classes.textRight}>
        <Typography variant="body1" className={classes.header}>
          {t('newRestoreModal.backupInfoBanner.createdAt')}
        </Typography>
        <div>{ybFormatDate(backupDetails!.commonBackupInfo.createTime)}</div>
      </Grid>
    </Grid>
  );
};

export default BackupInfoBanner;
