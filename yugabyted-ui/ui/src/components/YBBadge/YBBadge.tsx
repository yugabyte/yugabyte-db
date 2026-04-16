import React, { FC, ReactNode } from "react";
import clsx from "clsx";
import { makeStyles } from "@material-ui/core";
import WarningIcon from "@app/assets/alert-solid.svg";
import ErrorIcon from "@app/assets/failed-solid.svg";
import SuccessIcon from "@app/assets/check-badge.svg";
import InfoIcon from "@app/assets/info.svg";
import LoadingIcon from "@app/assets/Default-Loading-Circles.svg";

export enum BadgeVariant {
  Light = "light",
  Info = "info",
  Warning = "warning",
  Error = "error",
  Success = "success",
  InProgress = "inprogress",
}

export interface BadgeProps {
  text?: string | ReactNode;
  variant?: BadgeVariant;
  icon?: boolean;
  iconComponent?: typeof WarningIcon;
  className?: string;
  noText?: boolean;
  style?: React.CSSProperties;
}

const useStyles = makeStyles((theme) => ({
  root: () => ({
    display: "inline-flex",
    height: theme.spacing(3),
    padding: theme.spacing(0.5, 0.75),
    justifyContent: "center",
    alignItems: "center",
    gap: theme.spacing(0.25),
    borderRadius: theme.spacing(0.75),
    fontFamily: theme.typography.fontFamily,
    fontSize: `${theme.typography.subtitle1.fontSize}px !important`,
    fontStyle: "normal",
    fontWeight: theme.typography.body2.fontWeight,
    lineHeight: "16px",
    "& span": {
      fontSize: `${theme.typography.subtitle1.fontSize}px !important`,
      fontFamily: theme.typography.fontFamily,
      fontStyle: "normal",
      fontWeight: theme.typography.body2.fontWeight,
      lineHeight: "16px",
    },
    "& .MuiTypography-root": {
      fontSize: `${theme.typography.subtitle1.fontSize}px !important`,
      fontFamily: theme.typography.fontFamily,
      fontStyle: "normal",
      fontWeight: theme.typography.body2.fontWeight,
      lineHeight: "16px",
    }
  }),
  icon: {
    height: theme.spacing(1.75),
    width: theme.spacing(1.75),
  },
  light: {
    background: theme.palette.primary[100],
    color: theme.palette.primary[600],
  },
  lightIcon: {
    color: theme.palette.primary[700],
  },
  warning: {
    background: "var(--Warning-Warning-100, #FFF3DC)",
    color: theme.palette.warning[900],
  },
  warningIcon: {
    color: theme.palette.warning[500],
  },
  info: {
    background: "var(--Info-Info-100, #EBF1FF)",
    color: theme.palette.info[900],
  },
  infoIcon: {
    color: theme.palette.info[700],
  },
  success: {
    background: "var(--Success-Success-100, #CDEFE1)",
    color: theme.palette.success[700],
  },
  successIcon: {
    color: theme.palette.success[700],
  },
  error: {
    background: "var(--Error-Error-100, #FFE5E5)",
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
  iconOnly: {
    minWidth: 24,
    width: 24,
    height: 24,
    padding: 0,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
  },
}));

export const YBBadge: FC<BadgeProps> = (props: BadgeProps) => {
  const {
    text,
    variant = BadgeVariant.Info,
    icon = true,
    iconComponent: CustomIcon,
    className,
    noText = false,
    style,
  } = props;

  const classes = useStyles();
  let alertClassName = classes.root;
  let alertIcon = <span />;
  let alertText = text;
  switch (variant) {
    case BadgeVariant.Warning:
      alertClassName = clsx(alertClassName, classes.warning);
      alertIcon = <WarningIcon className={clsx(classes.icon, classes.warningIcon)} />;
      alertText = text ?? "Warning";
      break;
    case BadgeVariant.Success:
      alertClassName = clsx(alertClassName, classes.success);
      alertIcon = <SuccessIcon className={clsx(classes.icon, classes.successIcon)} />;
      alertText = text ?? "Success";
      break;
    case BadgeVariant.Error:
      alertClassName = clsx(alertClassName, classes.error);
      alertIcon = <ErrorIcon className={clsx(classes.icon, classes.errorIcon)} />;
      alertText = text ?? "Error";
      break;
    case BadgeVariant.InProgress:
      alertClassName = clsx(alertClassName, classes.inprogress);
      alertIcon = <LoadingIcon className={clsx(classes.icon, classes.inprogressIcon)} />;
      alertText = text ?? "In progress";
      break;
    case BadgeVariant.Light:
      alertClassName = clsx(alertClassName, classes.light);
      alertIcon = <span className={clsx(classes.icon, classes.lightIcon)} />;
      alertText = text ?? "Light";
      break;
    case BadgeVariant.Info:
    default:
      alertClassName = clsx(alertClassName, classes.info);
      alertIcon = <InfoIcon className={clsx(classes.icon, classes.infoIcon)} />;
      alertText = text ?? "Info";
      break;
  }

  if (noText) {
    alertClassName = clsx(alertClassName, classes.iconOnly, className);
  } else {
    alertClassName = clsx(alertClassName, className);
  }

  const getIcon = () => {
    if (!icon) {
      return null;
    }

    if (CustomIcon) {
      return <CustomIcon className={clsx(classes.icon, classes.inprogressIcon)} />;
    }
    return alertIcon;
  };

  return (
    <div className={alertClassName} style={style} role="alert" aria-label={`alert ${variant}`}>
      {!noText && alertText && <span>{alertText}</span>}
      {getIcon()}
    </div>
  );
};
