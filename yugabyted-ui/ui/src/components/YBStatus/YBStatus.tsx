import React, { FC, ReactNode } from "react";
import { FiberManualRecord } from "@material-ui/icons";
import { Box, createStyles, makeStyles, Theme } from "@material-ui/core";
import FailedIcon from "@app/assets/failed-solid.svg";
import CompletedIcon from "@app/assets/check.svg";
import SuccessIcon from "@app/assets/circle-check-solid.svg";
import LoadingIcon from "@app/assets/Default-Loading-Circles.svg";
import WarningIcon from "@app/assets/alert-solid.svg";
import ErrorIcon from "@app/assets/alert-solid.svg";
import { YBTooltip } from "../YBTooltip/YBTooltip";
import clsx from "clsx";

export enum STATUS_TYPES {
  SUCCESS = "success",
  FAILED = "failed",
  WARNING = "warning",
  COMPLETE = "completed",
  ACTIVE = "active",
  INACTIVE = "inactive",
  PENDING = "running",
  IN_PROGRESS = "in_progress",
  ERROR = "error",
}

interface StatusProps {
  label?: ReactNode;
  type?: STATUS_TYPES;
  value?: number;
  size?: number;
  tooltip?: boolean;
}

const useStyles = makeStyles<Theme, { size?: number }>((theme: Theme) => {
  return createStyles({
    root: {
      textAlign: "center",
      "& > span": {
        margin: theme.spacing(1),
      },
    },
    icon: ({ size }) =>
      size
        ? {
            width: size,
            height: size,
          }
        : {},
    colorCompleted: {
      color: theme.palette.success.main,
    },
    colorSuccess: {
      width: 24,
      color: theme.palette.success.main,
    },
    colorError: {
      width: 24,
      color: theme.palette.error.main,
    },
    colorWarning: {
      width: 24,
      color: theme.palette.warning.main,
    },
    colorInactive: {
      width: 12,
      color: theme.palette.grey[500],
    },
    colorActive: {
      width: 12,
      color: theme.palette.success.main,
    },
    colorFailed: {
      width: 24,
      color: theme.palette.error.main,
    },
    colorPending: {
      width: 12,
      color: theme.palette.warning[700],
    },
    loadingIcon: {
      width: 24,
      height: 24,
      margin: 0,
    },
  });
});

export const YBStatus: FC<StatusProps> = ({
  label,
  tooltip,
  value,
  size,
  type = STATUS_TYPES.COMPLETE,
}: StatusProps) => {
  const classes = useStyles({ size });

  const getTooltip = () => {
    if (!tooltip) {
      return "";
    }

    switch (type) {
      case STATUS_TYPES.FAILED: {
        return "Down";
      }
      case STATUS_TYPES.ACTIVE: {
        return "Active";
      }
      case STATUS_TYPES.INACTIVE: {
        return "Inactive";
      }
      case STATUS_TYPES.SUCCESS: {
        return "Running";
      }
      case STATUS_TYPES.WARNING: {
        return "Under-replicated";
      }
      case STATUS_TYPES.PENDING: {
        return "Pending";
      }
      case STATUS_TYPES.IN_PROGRESS: {
        return "Bootstrapping";
      }
      case STATUS_TYPES.ERROR: {
        return "Error";
      }
      default: {
        return "Completed";
      }
    }
  };

  const getIcon = () => {
    switch (type) {
      case STATUS_TYPES.FAILED: {
        return <FailedIcon className={clsx(classes.colorFailed, classes.icon)} />;
      }
      case STATUS_TYPES.ACTIVE: {
        return <FiberManualRecord className={clsx(classes.colorActive, classes.icon)} />;
      }
      case STATUS_TYPES.INACTIVE: {
        return <FiberManualRecord className={clsx(classes.colorInactive, classes.icon)} />;
      }
      case STATUS_TYPES.SUCCESS: {
        return <SuccessIcon className={clsx(classes.colorSuccess, classes.icon)} />;
      }
      case STATUS_TYPES.WARNING: {
        return <WarningIcon className={clsx(classes.colorWarning, classes.icon)} />;
      }
      case STATUS_TYPES.PENDING: {
        return <FiberManualRecord className={clsx(classes.colorPending, classes.icon)} />;
      }
      case STATUS_TYPES.IN_PROGRESS: {
        return <LoadingIcon className={clsx(classes.loadingIcon, classes.icon)} />;
      }
      case STATUS_TYPES.ERROR: {
        return <ErrorIcon className={clsx(classes.colorError, classes.icon)} />;
      }
      default: {
        return <CompletedIcon className={clsx(classes.colorCompleted, classes.icon)} />;
      }
    }
  };

  if (value === 0) {
    return null;
  }

  return (
    <Box display="flex" alignItems="center" justifyContent="center">
      <YBTooltip
        title={getTooltip()}
        children={
          <div
            style={{
              lineHeight: 0.2,
              marginRight: "6px",
              minWidth: "24px",
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
            }}
          >
            {getIcon()}
          </div>
        }
      ></YBTooltip>
      {label && <Box minWidth={35}>{label}</Box>}
    </Box>
  );
};
