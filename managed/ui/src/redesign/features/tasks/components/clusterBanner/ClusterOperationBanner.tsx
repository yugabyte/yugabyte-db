/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, ReactNode } from 'react';
import clsx from 'clsx';
import { Box, IconButton, Typography, makeStyles } from '@material-ui/core';
import { Close as CloseIcon } from '@material-ui/icons';

import {
  YBProgress,
  YBProgressBarState
} from '@app/redesign/components/YBProgress/YBLinearProgress';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';

import AlertTriangleIcon from '@app/redesign/assets/alert.svg';
import SuccessIcon from '@app/redesign/assets/circle-check.svg';
import InfoIcon from '@app/redesign/assets/info.svg';
import LoadingIcon from '@app/redesign/assets/default-loading-circles.svg';
import ErrorIcon from '@app/redesign/assets/approved/error.svg';

export const ClusterOperationBannerType = {
  INFO: 'info',
  ALERT: 'alert',
  ERROR: 'error',
  IN_PROGRESS: 'inProgress',
  PENDING_ACTION_WHITE: 'pendingActionWhite',
  PENDING_ACTION_YELLOW: 'pendingActionYellow',
  SUCCESS: 'success'
} as const;
export type ClusterOperationBannerType =
  (typeof ClusterOperationBannerType)[keyof typeof ClusterOperationBannerType];

export interface ClusterOperationBannerProps {
  type: ClusterOperationBannerType;
  title: ReactNode;
  description?: ReactNode;
  icon?: ReactNode;
  progressPercent?: number;
  actions?: ReactNode;
  onDismiss?: () => void;
  className?: string;
  dataTestId?: string;
}

export const useClusterOperationBannerStyles = makeStyles((theme) => ({
  banner: {
    display: 'flex',
    flexDirection: 'row',
    gap: theme.spacing(2),
    alignItems: 'center',

    width: '100%',
    height: 46,
    padding: theme.spacing(2, 1),

    borderRadius: theme.shape.borderRadius,

    '&$info': {
      background: theme.palette.info[100],
      border: `1px solid ${theme.palette.info[200]}`,

      '& $icon': {
        color: theme.palette.info[600]
      }
    },
    '&$inProgress': {
      background: theme.palette.primary[100],
      border: `1px solid ${theme.palette.primary[200]}`,

      '& $icon': {
        color: theme.palette.primary[600]
      }
    },
    '&$alert': {
      background: theme.palette.warning[50],
      border: `1px solid ${theme.palette.warning[100]}`,

      '& $icon': {
        color: theme.palette.warning[500]
      }
    },
    '&$error': {
      background: theme.palette.error[50],
      border: `1px solid ${theme.palette.error[100]}`,

      '& $icon': {
        color: theme.palette.error[500]
      }
    },
    '&$pendingActionWhite': {
      background: theme.palette.common.white,
      border: `1px solid ${theme.palette.grey[200]}`,

      '& $icon': {
        color: theme.palette.grey[600]
      }
    },
    '&$pendingActionYellow': {
      background: theme.palette.warning[50],
      border: `1px solid ${theme.palette.warning[100]}`,

      '& $icon': {
        color: theme.palette.warning[500]
      }
    },
    '&$success': {
      background: theme.palette.success[50],
      border: `1px solid ${theme.palette.success[100]}`,

      '& $icon': {
        color: theme.palette.success[500]
      }
    }
  },
  icon: {
    width: 24,
    height: 24,
    minWidth: 24,
    minHeight: 24,

    lineHeight: '100%',
    fontSize: 24,
    fontWeight: 600
  },
  textContainer: {
    lineHeight: '16px'
  },
  title: {
    display: 'inline',
    fontWeight: 600
  },
  descriptionInline: {
    display: 'inline',

    marginLeft: theme.spacing(0.5)
  },
  progressContainer: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),

    lineHeight: '16px'
  },
  actionsSlot: {
    display: 'flex',
    flexShrink: 0,
    flexDirection: 'row',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  dismissButton: {
    flexShrink: 0,
    marginLeft: 'auto'
  },
  closeIcon: {
    height: 12,
    width: 12,
    fill: theme.palette.text.primary,
    cursor: 'pointer'
  },
  info: {},
  inProgress: {},
  alert: {},
  error: {},
  pendingActionWhite: {},
  pendingActionYellow: {},
  success: {}
}));

function toneModifierClass(
  classes: ReturnType<typeof useClusterOperationBannerStyles>,
  type: ClusterOperationBannerType
): string {
  switch (type) {
    case ClusterOperationBannerType.ALERT:
      return classes.alert;
    case ClusterOperationBannerType.ERROR:
      return classes.error;
    case ClusterOperationBannerType.IN_PROGRESS:
      return classes.inProgress;
    case ClusterOperationBannerType.PENDING_ACTION_WHITE:
      return classes.pendingActionWhite;
    case ClusterOperationBannerType.PENDING_ACTION_YELLOW:
      return classes.pendingActionYellow;
    case ClusterOperationBannerType.SUCCESS:
      return classes.success;
    case ClusterOperationBannerType.INFO:
      return classes.info;
    default:
      return assertUnreachableCase(type);
  }
}

function progressStateForTone(type: ClusterOperationBannerType): YBProgressBarState {
  switch (type) {
    case ClusterOperationBannerType.ERROR:
      return YBProgressBarState.Error;
    case ClusterOperationBannerType.ALERT:
    case ClusterOperationBannerType.SUCCESS:
    case ClusterOperationBannerType.PENDING_ACTION_WHITE:
    case ClusterOperationBannerType.PENDING_ACTION_YELLOW:
    case ClusterOperationBannerType.INFO:
    case ClusterOperationBannerType.IN_PROGRESS:
      return YBProgressBarState.InProgress;
    default:
      return assertUnreachableCase(type);
  }
}

export const ClusterOperationBanner: FC<ClusterOperationBannerProps> = ({
  type,
  title,
  description,
  icon,
  progressPercent,
  actions,
  onDismiss,
  className,
  dataTestId
}) => {
  const classes = useClusterOperationBannerStyles();

  const defaultIcon = (() => {
    switch (type) {
      case ClusterOperationBannerType.IN_PROGRESS:
        return <LoadingIcon className={classes.icon} />;
      case ClusterOperationBannerType.ALERT:
        return <AlertTriangleIcon className={classes.icon} />;
      case ClusterOperationBannerType.ERROR:
        return <ErrorIcon className={classes.icon} />;
      case ClusterOperationBannerType.SUCCESS:
        return <SuccessIcon className={classes.icon} />;
      case ClusterOperationBannerType.PENDING_ACTION_WHITE:
        return <div className={classes.icon}>👋</div>;
      case ClusterOperationBannerType.PENDING_ACTION_YELLOW:
        return <div className={classes.icon}>👋</div>;
      case ClusterOperationBannerType.INFO:
        return <InfoIcon className={classes.icon} />;
      default:
        return assertUnreachableCase(type);
    }
  })();

  const resolvedIcon = icon ?? defaultIcon;

  return (
    <div
      className={clsx(classes.banner, toneModifierClass(classes, type), className)}
      data-testid={dataTestId}
    >
      {resolvedIcon}

      <span className={classes.textContainer}>
        <Typography variant="body1" component="span" className={classes.title}>
          {title}
        </Typography>
        {description ? (
          <Typography variant="body2" component="span" className={classes.descriptionInline}>
            {description}
          </Typography>
        ) : null}
      </span>

      {typeof progressPercent === 'number' ? (
        <Box className={classes.progressContainer}>
          <Typography variant="body1" component="span">
            {Math.trunc(progressPercent)}%
          </Typography>
          <YBProgress
            state={progressStateForTone(type)}
            value={progressPercent}
            height={8}
            width={130}
          />
        </Box>
      ) : null}

      {actions ? <Box className={classes.actionsSlot}>{actions}</Box> : null}

      {onDismiss ? (
        <IconButton
          aria-label="Dismiss"
          className={classes.dismissButton}
          size="small"
          onClick={onDismiss}
        >
          <CloseIcon className={classes.closeIcon} />
        </IconButton>
      ) : null}
    </div>
  );
};
