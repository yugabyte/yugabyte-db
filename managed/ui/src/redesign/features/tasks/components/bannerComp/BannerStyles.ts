/*
 * Created on Fri Dec 22 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { makeStyles } from '@material-ui/core';

export const useBannerCommonStyles = makeStyles((theme) => ({
  viewDetsailsButton: {
    color: theme.palette.ybacolors.ybDarkGray,
    fontSize: '12px',
    fontWeight: 500,
    height: '30px',
    '& .MuiButton-label': {
      fontSize: '12px'
    }
  },
  flex: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px'
  },
  divider: {
    width: '1px',
    height: '24px',
    background: theme.palette.ybacolors.ybBorderGray,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  }
}));
