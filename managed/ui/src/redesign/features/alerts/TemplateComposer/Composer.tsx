/*
 * Created on Tue Feb 14 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useState } from 'react';
import { Grid, makeStyles } from '@material-ui/core';
import { Descendant } from 'slate';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import CustomVariablesEditor from './CustomVariables';
import { YBEditor } from '../../../components/YBEditor';
import { YBButton } from '../../../components';
import { createAlertChannelTemplates } from './CustomVariablesAPI';

type ComposerProps = {};

const useStyles = makeStyles((theme) => ({
  root: {
    height: '600px'
  },
  editor: {
    width: '100%',
    height: '100%',
    border: 'none'
  },
  content: {
    marginTop: theme.spacing(2.5)
  },
  actions: {
    background: '#F5F4F0',
    padding: theme.spacing(2),
    boxShadow: `0px -1px 0px rgba(0, 0, 0, 0.1)`
  },
  submitButton: {
    width: '65px !important',
    height: '40px'
  }
}));

const Composer = (props: ComposerProps) => {
  const classes = useStyles(props);
  const [subject, setSubject] = useState<Descendant[]>([]);
  const [body, setBody] = useState<Descendant[]>([]);

  const createTemplate = useMutation(() => {
    return createAlertChannelTemplates({
      type: 'Email',
      textTemplate: JSON.stringify(body),
      titleTemplate: JSON.stringify(subject)
    });
  });

  const { t } = useTranslation();

  return (
    <Grid container spacing={2} className={classes.root}>
      <Grid item xs={9} sm={9} lg={9} md={9}>
        <Grid>
          <Grid item>
            {t('alertCustomTemplates.composer.subject')}
            <YBEditor singleLine={true} showToolbar={false} setVal={setSubject} />
          </Grid>
          <Grid item className={classes.content}>
            {t('alertCustomTemplates.composer.content')}
            <YBEditor showToolbar={true} setVal={setBody} />
          </Grid>
        </Grid>
        <Grid container className={classes.actions} alignItems="center" justifyContent="flex-end">
          <YBButton
            variant="primary"
            type="submit"
            autoFocus
            className={classes.submitButton}
            onClick={() => createTemplate.mutate()}
          >
            {t('common.save')}
          </YBButton>
        </Grid>
      </Grid>
      <Grid item xs={3} sm={3} lg={3} md={3} container justifyContent="center">
        <CustomVariablesEditor />
      </Grid>
    </Grid>
  );
};

export default Composer;
