/*
 * Created on Fri Dec 22 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { LinearProgress, withStyles } from '@material-ui/core';

export enum YBProgressBarState {
  Error = 'error',
  Success = 'success',
  InProgress = 'inProgress',
  Unknown = 'unknown',
  Warning = 'Warning'
}

interface YBProgressProps {
  state: YBProgressBarState;
  value: number;
  height?: number;
  width?: number;
}

export const YBProgress: FC<YBProgressProps> = ({ state, value, height, width }) => {
  const Progress = withStyles((theme) => ({
    root: {
      height: height ?? 10,
      borderRadius: (height ?? 10) / 2,
      width: width ?? '100%'
    },
    colorPrimary: {
      backgroundColor: () => {
        return theme.palette.ybacolors.ybBorderGray;
      }
    },
    bar: {
      borderRadius: height ?? 10,
      backgroundColor: () => {
        switch (state) {
          case YBProgressBarState.Error:
            return theme.palette.ybacolors.error;
          case YBProgressBarState.Success:
            return '#13A768';
          case YBProgressBarState.InProgress:
            return '#1890FF';
          case YBProgressBarState.Warning:
            return theme.palette.ybacolors.warning;
          case YBProgressBarState.Unknown:
          default:
            return theme.palette.ybacolors.ybDarkGray;
        }
      }
    }
  }))(LinearProgress);

  return <Progress value={value} variant="determinate" />;
};
