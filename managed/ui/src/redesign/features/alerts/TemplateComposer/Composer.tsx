/*
 * Created on Tue Feb 14 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React from 'react';
import { Grid, makeStyles } from '@material-ui/core';
import CustomVariablesEditor from './CustomVariables';

type ComposerProps = {};

const useStyles = makeStyles(() => ({
  root: {
    background: '#F6F6F6',
    height: '750px',
    boxShadow: '0 4px 10px rgba(0, 0, 0, 0.25)'
  },
  editor: {
    width: '100%',
    height: '100%',
    border: 'none'
  }
}));

const Composer = (props: ComposerProps) => {
  const classes = useStyles(props);
  return (
    <Grid container spacing={2} className={classes.root}>
      <Grid item xs={9} sm={9} lg={9} md={9}>
        <textarea className={classes.editor} />
      </Grid>
      <Grid item xs={3} sm={3} lg={3} md={3} container justifyContent="center">
        <CustomVariablesEditor />
      </Grid>
    </Grid>
  );
};

export default Composer;
