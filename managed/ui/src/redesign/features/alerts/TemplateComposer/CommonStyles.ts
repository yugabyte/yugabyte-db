/*
 * Created on Thu Feb 16 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { makeStyles } from '@material-ui/core';

/**
 * default styles for Composer components
 */
export const useCommonStyles = makeStyles((theme) => ({
  defaultBorder: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  helpText: {
    fontFamily: 'Inter',
    fontWeight: 400,
    lineHeight: `${theme.spacing(2)}px`,
    color: '#67666C'
  },
  formErrText: {
    color: theme.palette.error[500],
    margin: 0,
    fontSize: '11.5px',
    fontWeight: 400,
    lineHeight: `${theme.spacing(2)}px`
  },
  menuStyles: {
    '& .MuiMenuItem-root': {
      height: '50px',
      minWidth: '190px'
    },
    '& .MuiMenuItem-root svg': {
      marginRight: theme.spacing(1.4),
      color: theme.palette.orange[500],
      height: '20px',
      width: '20px'
    }
  },
  menuNoBorder: {
    '& .MuiMenu-paper': {
      border: 'none'
    }
  },
  clickable: {
    cursor: 'pointer'
  },
  subText: {
    fontWeight: 500,
    fontSize: '11.5px',
    lineHeight: `${theme.spacing(2)}px`,
    color: 'rgba(35, 35, 41, 0.4)'
  },
  upperCase: {
    textTransform: 'uppercase'
  },
  editorBorder: {
    border: '1px solid #E5E5E6',
    borderRadius: `${theme.spacing(1)}px`
  },
  editor: {
    width: '100%',
    height: '100%',
    border: 'none'
  },
  subjectEditor: {
    height: '50px',
    padding: `0 ${theme.spacing(2)}px`
  },
  noPadding: {
    padding: '0 !important'
  },
  noOverflow: {
    overflow: 'hidden'
  }
}));
