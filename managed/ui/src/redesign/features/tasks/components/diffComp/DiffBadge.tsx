/*
 * Created on Thu May 16 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { DiffOperation } from './dtos';
import { Typography, makeStyles } from '@material-ui/core';
import { ReactComponent as CheckIcon } from '../../../../assets/check.svg';
import { ReactComponent as DeleteIcon } from '../../../../assets/Close-Bold.svg';
import { ReactComponent as ChangeIcon } from '../../../../assets/switch-icon.svg';

interface DiffBadgeProps {
  type: DiffOperation;
  minimal?: boolean;
  hideIcon?: boolean;
  customText?: string | JSX.Element;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `2px 6px`,
    height: theme.spacing(3),
    display: 'inline-flex',
    alignItems: 'center',
    justifyContent: 'center',
    borderRadius: '6px',
    gap: '4px',
    lineHeight: theme.spacing(2),
    '& > svg': {
      width: theme.spacing(2),
      height: theme.spacing(2)
    }
  },
  minimal: {
    width: theme.spacing(3),
    padding: 0
  },
  Added: {
    background: theme.palette.success[100],
    color: theme.palette.success[700]
  },
  Changed: {
    background: theme.palette.warning[100],
    color: theme.palette.warning[900]
  },
  Removed: {
    background: theme.palette.grey[200],
    color: theme.palette.ybacolors.textDarkGray
  },
  bold: {
    fontWeight: 500
  }
}));

const DiffBadge: FC<DiffBadgeProps> = ({ minimal = false, type, hideIcon = false, customText }) => {
  const classes = useStyles();
  let Icon = CheckIcon;
  if (type === DiffOperation.REMOVED) {
    Icon = DeleteIcon;
  } else if (type === DiffOperation.CHANGED) {
    Icon = ChangeIcon;
  }
  return (
    <div className={clsx(classes.root, minimal && classes.minimal, classes[type])}>
      {!hideIcon && <Icon />}
      {!minimal && (
        <Typography variant="subtitle1" className={classes.bold}>
          {customText ?? type}
        </Typography>
      )}
    </div>
  );
};

export default DiffBadge;
