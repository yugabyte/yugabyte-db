/*
 * Created on Mon Feb 27 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Table, TableCell, TableHead, TableRow, withStyles } from '@material-ui/core';

export const YBTable = withStyles((theme) => ({
  root: {
    borderRadius: theme.spacing(1),
    borderCollapse: 'inherit',
    width: '100%'
  }
}))(Table);

export const YBTableRow = withStyles(() => ({
  root: {
    '& td:nth-child(1),th:nth-child(1)': {
      position: 'sticky',
      background: '#fff',
      left: 0,
      zIndex: 1,
      width: '350px'
    },
    '& th': {
      background: '#f9f8f8 !important'
    },
    '& th:nth-child(1)': {
      zIndex: 3,
      top: 0
    }
  }
}))(TableRow);

export const YBTableHead = withStyles((theme) => ({
  root: {
    background: '#e9e8e8',
    height: theme.spacing(6.9)
  }
}))(TableHead);

export const YBTableCell = withStyles((theme) => ({
  root: {
    padding: theme.spacing(1.5),
    minWidth: '200px',
    height: theme.spacing(7),
    borderRight: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  }
}))(TableCell);
