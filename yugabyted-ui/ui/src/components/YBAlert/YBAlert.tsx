import React, { FC, ReactNode } from 'react';
import clsx from 'clsx';
import { makeStyles, Snackbar, Fade } from '@material-ui/core';
import { Close as CloseIcon } from '@material-ui/icons';
import WarningIcon from '@app/assets/alert.svg';
import ErrorIcon from '@app/assets/warning.svg';
import SuccessIcon from '@app/assets/circle-check.svg';
import InfoIcon from '@app/assets/info.svg';

export enum AlertVariant {
  Info = 'info',
  Warning = 'warning',
  Error = 'error',
  Success = 'success'
}

const DEFAULT_AUTO_DISMISS_MS = 6000;

export interface AlertProps {
  open: boolean;
  text: string | ReactNode;
  onClose?: () => void;
  autoDismiss?: number;
  width?: number;
  isToast?: boolean;
  variant?: AlertVariant;
  key?: string | number;
  position?: number;
}

const useStyles = makeStyles((theme) => ({
  root: ({ position }: AlertProps) => ({
    padding: theme.spacing(1),
    borderRadius: theme.shape.borderRadius,
    display: 'flex',
    alignItems: 'center',
    marginTop: (position ?? 0) * 58
  }),
  icon: {
    width: theme.spacing(3),
    height: theme.spacing(3),
    minWidth: theme.spacing(3),
    minHeight: theme.spacing(3)
  },
  warning: {
    background: theme.palette.warning[100]
  },
  info: {
    background: theme.palette.info[100]
  },
  success: {
    background: theme.palette.success[100]
  },
  error: {
    background: theme.palette.error[100]
  },
  toastWarning: {
    padding: theme.spacing(1.5, 1),
    borderWidth: '1px',
    borderStyle: 'solid',
    borderColor: theme.palette.warning[300],
    boxShadow: theme.shape.shadowLight,
    minWidth: '200px'
  },
  toastInfo: {
    padding: theme.spacing(1.5, 1),
    borderWidth: '1px',
    borderStyle: 'solid',
    borderColor: theme.palette.info[300],
    boxShadow: theme.shape.shadowLight,
    minWidth: '200px'
  },
  toastSuccess: {
    padding: theme.spacing(1.5, 1),
    borderWidth: '1px',
    borderStyle: 'solid',
    borderColor: theme.palette.success[300],
    boxShadow: theme.shape.shadowLight,
    minWidth: '200px'
  },
  toastError: {
    padding: theme.spacing(1.5, 1),
    borderWidth: '1px',
    borderStyle: 'solid',
    borderColor: theme.palette.error[300],
    boxShadow: theme.shape.shadowLight,
    minWidth: '200px'
  },
  text: {
    marginLeft: theme.spacing(1),
    marginRight: 'auto'
  },
  close: {
    height: theme.spacing(1.5),
    width: theme.spacing(1.5),
    fill: theme.palette.text.primary,
    marginRight: theme.spacing(0.5),
    marginLeft: theme.spacing(1.5),
    cursor: 'pointer'
  },
  warningIcon: {
    color: theme.palette.warning[700]
  },
  infoIcon: {
    color: theme.palette.info[700]
  },
  successIcon: {
    color: theme.palette.success[700]
  },
  errorIcon: {
    color: theme.palette.error[700]
  }
}));

export const YBAlert: FC<AlertProps> = (props: AlertProps) => {
  const {
    open,
    text,
    autoDismiss,
    onClose,
    variant = AlertVariant.Info,
    isToast = false,
    width,
    ...alertProps
  } = props;
  const classes = useStyles(props);
  const alertWidth = width === undefined ? 'auto' : `${width}px`;
  let alertClassName = classes.root;
  let alertIcon = <span />;
  switch (variant) {
    case AlertVariant.Warning:
      alertClassName = clsx(alertClassName, classes.warning, isToast && classes.toastWarning);
      alertIcon = <WarningIcon className={clsx(classes.icon, classes.warningIcon)} />;
      break;
    case AlertVariant.Success:
      alertClassName = clsx(alertClassName, classes.success, isToast && classes.toastSuccess);
      alertIcon = <SuccessIcon className={clsx(classes.icon, classes.successIcon)} />;
      break;
    case AlertVariant.Error:
      alertClassName = clsx(alertClassName, classes.error, isToast && classes.toastError);
      alertIcon = <ErrorIcon className={clsx(classes.icon, classes.errorIcon)} />;
      break;
    case AlertVariant.Info:
    default:
      alertClassName = clsx(alertClassName, classes.info, isToast && classes.toastInfo);
      alertIcon = <InfoIcon className={clsx(classes.icon, classes.infoIcon)} />;
      break;
  }

  const handleClose = (_event: unknown, reason: string) => {
    if (reason === 'clickaway') {
      return;
    }
    if (onClose) {
      onClose();
    }
  };

  if (isToast) {
    return (
      <Snackbar
        open={open}
        autoHideDuration={autoDismiss ?? DEFAULT_AUTO_DISMISS_MS}
        TransitionComponent={Fade}
        anchorOrigin={{ vertical: 'top', horizontal: 'center' }}
        onClose={handleClose}
        {...alertProps}
      >
        <div className={alertClassName} role="alert" aria-label={`alert ${variant}`} style={{ width: alertWidth }}>
          {alertIcon}
          <span className={classes.text}>{text}</span>
          {onClose && <CloseIcon onClick={onClose} className={classes.close} />}
        </div>
      </Snackbar>
    );
  }
  return (
    <Fade in={open}>
      <div className={alertClassName} role="alert" aria-label={`alert ${variant}`} style={{ width: alertWidth }}>
        {alertIcon}
        <span className={classes.text}>{text}</span>
        {onClose && <CloseIcon onClick={onClose} className={classes.close} />}
      </div>
    </Fade>
  );
};
