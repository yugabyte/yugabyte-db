/*
 * Created on Wed Jul 12 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import { YBButton } from '../../../components';

const useStyles = makeStyles((theme) => ({
  footerAction: {
    height: '74px',
    display: 'flex',
    justifyContent: 'flex-end',
    alignItems: 'flex-start',
    background: '#F5F4F0',
    boxShadow: '0px -1px 0px 0px rgba(0, 0, 0, 0.10)',
    gap: '15px',
    padding: `${theme.spacing(2.25)}px ${theme.spacing(3.75)}px`,
    position: 'relative',
    bottom: 0,
    width: '100%',
    '& button': {
      height: theme.spacing(5)
    }
  }
}));

type props = {
  children?: React.ReactElement;
  onSave: () => void;
  onCancel: () => void;
  saveLabel?: string;
};

const Container: FC<props> = ({ children, onSave, onCancel, saveLabel }) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'common'
  });

  if (!children) return null;

  return (
    <Box>
      <Box>{children}</Box>
      <Box className={classes.footerAction}>
        <YBButton variant="secondary" onClick={onCancel}>
          {t('cancel')}
        </YBButton>
        <YBButton variant="primary" onClick={onSave}>
          {saveLabel ?? t('save')}
        </YBButton>
      </Box>
    </Box>
  );
};

export default Container;
