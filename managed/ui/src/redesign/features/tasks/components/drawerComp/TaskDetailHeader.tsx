/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { Typography, makeStyles } from '@material-ui/core';

const useStyles = makeStyles((theme) => ({
  header: {
    height: '68px',
    padding: '24px 20px',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    borderBottom: `2px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  closeIcon: {
    fontSize: '20px',
    color: theme.palette.ybacolors.ybIcon,
    cursor: 'pointer'
  }
}));

export interface TaskDetailsHeaderProps {
  onClose: () => void;
}

export const TaskDetailsHeader: FC<TaskDetailsHeaderProps> = ({ onClose }) => {
  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.header'
  });

  return (
    <div className={classes.header}>
      <Typography variant="h4">{t('title')}</Typography>
      <i className={clsx('fa fa-close', classes.closeIcon)} onClick={() => onClose()} />
    </div>
  );
};
