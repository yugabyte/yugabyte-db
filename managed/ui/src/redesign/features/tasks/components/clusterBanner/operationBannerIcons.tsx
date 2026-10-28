/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { makeStyles } from '@material-ui/core';

import LoadingIcon from '@app/redesign/assets/default-loading-circles.svg';

const useStyles = makeStyles(() => ({
  icon: {
    width: 24,
    height: 24,
    minWidth: 24,
    minHeight: 24,

    lineHeight: '100%',
    fontSize: 24,
    fontWeight: 600
  }
}));

export const OperationBannerLoadingIcon = () => {
  const classes = useStyles();
  return <LoadingIcon className={classes.icon} />;
};

export const OperationBannerWaveIcon = () => {
  const classes = useStyles();
  return <div className={classes.icon}>👋</div>;
};
