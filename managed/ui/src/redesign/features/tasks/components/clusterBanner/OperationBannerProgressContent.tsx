/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Box, Typography, makeStyles } from '@material-ui/core';

import {
  YBProgress,
  YBProgressBarState
} from '@app/redesign/components/YBProgress/YBLinearProgress';

const useStyles = makeStyles((theme) => ({
  progressContainer: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),

    lineHeight: '16px'
  }
}));

interface OperationBannerProgressContentProps {
  progressPercent: number;
  state?: YBProgressBarState;
}

export const OperationBannerProgressContent = ({
  progressPercent,
  state = YBProgressBarState.InProgress
}: OperationBannerProgressContentProps) => {
  const classes = useStyles();

  return (
    <Box className={classes.progressContainer}>
      <Typography variant="body1" component="span">
        {Math.trunc(progressPercent)}%
      </Typography>
      <YBProgress state={state} value={progressPercent} height={8} width={130} />
    </Box>
  );
};
