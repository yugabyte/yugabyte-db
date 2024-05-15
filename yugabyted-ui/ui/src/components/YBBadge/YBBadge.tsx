import React, { FC, ReactNode } from 'react';
import clsx from 'clsx';
import { makeStyles } from '@material-ui/core';
import WarningIcon from '@app/assets/alert-solid.svg';
import ErrorIcon from '@app/assets/failed-solid.svg';
import SuccessIcon from '@app/assets/check-badge.svg';
import InfoIcon from '@app/assets/info.svg';
import LoadingIcon from '@app/assets/Default-Loading-Circles.svg';

export enum BadgeVariant {
  Light = 'light',
  Info = 'info',
  Warning = 'warning',
  Error = 'error',
  Success = 'success',
  InProgress = 'inprogress',
}

export interface BadgeProps {
  text?: string | ReactNode;
  variant?: BadgeVariant;
  icon?: boolean,
}

const useStyles = makeStyles((theme) => ({
  root: ({ icon }: BadgeProps) => ({
    padding: icon ?
      `${theme.spacing(0.6)}px ${theme.spacing(1)}px` : `${theme.spacing(0.2)}px ${theme.spacing(0.8)}px`,
    borderRadius: icon ? theme.shape.borderRadius : theme.shape.borderRadius / 2,
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',
    width: 'fit-content',
    '& span:first-letter': {
      textTransform: 'uppercase',
    },
    minHeight: '24px'
  }),
  icon: {
    height: "14px",
    width: "14px",
  },
  light: {
    background: theme.palette.primary[100],
    color: theme.palette.primary[600],
  },
  lightIcon: {
    color: theme.palette.primary[700],
  },
  warning: {
    background: theme.palette.warning[100],
    color: theme.palette.warning[900],
  },
  warningIcon: {
    color: theme.palette.warning[500],
  },
  info: {
    background: theme.palette.info[100],
    color: theme.palette.info[900],
  },
  infoIcon: {
    color: theme.palette.info[700],
  },
  success: {
    background: theme.palette.success[100],
    color: theme.palette.success[900],
  },
  successIcon: {
    color: theme.palette.success[700],
  },
  error: {
    background: theme.palette.error[100],
    color: theme.palette.error[500],
  },
  errorIcon: {
    color: theme.palette.error[700],
  },
  inprogress: {
    background: theme.palette.primary[300],
    color: theme.palette.primary[700],
  },
  inprogressIcon: {
    color: theme.palette.primary[700],
  },
}));

export const YBBadge: FC<BadgeProps> = (props: BadgeProps) => {
  const {
    text,
    variant = BadgeVariant.Info,
    icon = true,
  } = props;

  const classes = useStyles({ ...props, icon });
  let alertClassName = classes.root;
  let alertIcon = <span />;
  let alertText = text;
  switch (variant) {
    case BadgeVariant.Warning:
      alertClassName = clsx(alertClassName, classes.warning);
      alertIcon = <WarningIcon className={clsx(classes.icon, classes.warningIcon)} />;
      alertText = text || "Warning";
      break;
    case BadgeVariant.Success:
      alertClassName = clsx(alertClassName, classes.success);
      alertIcon = <SuccessIcon className={clsx(classes.icon, classes.successIcon)} />;
      alertText = text || "Success";
      break;
    case BadgeVariant.Error:
      alertClassName = clsx(alertClassName, classes.error);
      alertIcon = <ErrorIcon className={clsx(classes.icon, classes.errorIcon)} />;
      alertText = text || "Error";
      break;
    case BadgeVariant.InProgress:
      alertClassName = clsx(alertClassName, classes.inprogress);
      alertIcon = <LoadingIcon className={clsx(classes.icon, classes.inprogressIcon)} />;
      alertText = text || "In progress";
      break;
    case BadgeVariant.Light:
      alertClassName = clsx(alertClassName, classes.light);
      alertIcon = <span className={clsx(classes.icon, classes.lightIcon)} />;
      alertText = text || "Light";
      break;
    case BadgeVariant.Info:
    default:
      alertClassName = clsx(alertClassName, classes.info);
      alertIcon = <InfoIcon className={clsx(classes.icon, classes.infoIcon)} />;
      alertText = text || "Info";
      break;
  }

  return (
    <div className={alertClassName} role="alert" aria-label={`alert ${variant}`}>
      <span>{alertText}</span>
      {icon && alertIcon}
    </div>
  );
};
