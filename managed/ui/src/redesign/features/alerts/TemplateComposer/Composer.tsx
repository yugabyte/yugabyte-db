/*
 * Created on Tue Feb 14 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useRef, useState } from 'react';
import { Divider, Grid, makeStyles, MenuItem, Select } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import CustomVariablesEditor from './CustomVariables';
import { useCommonStyles } from './CommonStyles';
import { ArrowBack } from '@material-ui/icons';
import { IComposerRef } from './composers/IComposer';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';

const EmailComposer = React.lazy(() => import('./composers/email'))
const WebhookComposer = React.lazy(() => import('./composers/webhook'))

type ComposerProps = {
  onHide: () => void;
};

const useStyles = makeStyles((theme) => ({
  root: {
    height: '650px',
    background: '#F6F6F6'
  },
  composerSelection: {
    padding: `${theme.spacing(3)}px ${theme.spacing(3.5)}px !important`
  },
  composerSelect: {
    border: 'none',
    boxShadow: 'none !important',
    "& .MuiInput-input": {
      fontWeight: 700,
      fontSize: '17px'
    },
    "& .MuiSelect-icon": {
      width: '25px',
      height: '25px',
      color: theme.palette.grey[900]
    }
  },
  composerDivider: {
    marginTop: theme.spacing(1.5)
  },
  backArrow: {
    color: theme.palette.orange[500],
    width: '20px',
    height: '20px',
    marginRight: theme.spacing(2),
    cursor: 'pointer'
  },

  composerArea: {
    background: theme.palette.common.white,
    padding: '0 !important'
  },


}));

const Composer: FC<ComposerProps> = ({ onHide }) => {
  const classes = useStyles();
  const commonStyles = useCommonStyles();
  const { t } = useTranslation();

  const composerRef = useRef<IComposerRef>(null);

  const [composer, setComposer] = useState('email');

  return (
    <Grid container spacing={2} className={classes.root}>
      <Grid item xs={9} sm={9} lg={9} md={9} className={classes.composerArea}>
        <Grid item xs={12} sm={12} lg={12} md={12} className={classes.composerSelection}>
          <Grid item container alignItems='center'>
            <ArrowBack className={classes.backArrow} onClick={onHide} />
            <Select
              id="composer-select"
              label="composer-selector"
              className={classes.composerSelect}
              value={composer}
              onChange={e => setComposer(e.target.value as string)}
              data-testid="select-Composer"
            >
              <MenuItem value={"email"} data-testid="email-composer" selected>{t('alertCustomTemplates.composer.emailTemplate')}</MenuItem>
              <MenuItem value={"webhook"} data-testid="webhook-composer">{t('alertCustomTemplates.composer.webhookTemplate')}</MenuItem>
            </Select>
          </Grid>
          <Divider className={classes.composerDivider} />
        </Grid>
        <React.Suspense fallback={<YBLoadingCircleIcon />}>
          {
            composer === 'email' ? (
              <EmailComposer ref={composerRef} onClose={onHide}/>
            ) : (
              <WebhookComposer ref={composerRef} onClose={onHide} />
            )
          }
        </React.Suspense>
      </Grid>
      <Grid item xs={3} sm={3} lg={3} md={3} container justifyContent="center" className={commonStyles.noPadding}>
        <CustomVariablesEditor />
      </Grid>
    </Grid>
  );
};

export default Composer;

