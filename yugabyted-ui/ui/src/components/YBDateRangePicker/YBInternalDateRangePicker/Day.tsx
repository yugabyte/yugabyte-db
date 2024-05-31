import React from 'react';
import { IconButton, Typography, makeStyles, Theme } from '@material-ui/core';
import clsx from 'clsx';

const useStyles = makeStyles((theme: Theme) => ({
  leftBorderRadius: {
    borderRadius: '50% 0 0 50%'
  },
  rightBorderRadius: {
    borderRadius: '0 50% 50% 0'
  },
  buttonContainer: {
    display: 'flex'
  },
  button: {
    height: theme.spacing(4.5),
    width: theme.spacing(4.5),
    padding: 0
  },
  buttonText: {
    lineHeight: 1.6
  },
  outlined: {
    border: `1px solid ${theme.palette.primary.dark}`
  },
  filled: {
    '&:hover': {
      backgroundColor: theme.palette.primary.dark
    },
    backgroundColor: theme.palette.primary.dark
  },
  highlighted: {
    backgroundColor: theme.palette.action.hover
  },
  contrast: {
    color: theme.palette.primary.contrastText
  }
}));

interface DayProps {
  filled?: boolean;
  outlined?: boolean;
  highlighted?: boolean;
  disabled?: boolean;
  startOfRange?: boolean;
  endOfRange?: boolean;
  onClick?: () => void;
  onHover?: () => void;
  value: number | string;
}

export const Day: React.FunctionComponent<DayProps> = ({
  startOfRange,
  endOfRange,
  disabled,
  highlighted,
  outlined,
  filled,
  onClick,
  onHover,
  value
}: DayProps) => {
  const classes = useStyles();

  return (
    <div
      className={clsx(
        classes.buttonContainer,
        startOfRange && classes.leftBorderRadius,
        endOfRange && classes.rightBorderRadius,
        !disabled && highlighted && classes.highlighted
      )}
    >
      <IconButton
        className={clsx(
          classes.button,
          !disabled && outlined && classes.outlined,
          !disabled && filled && classes.filled
        )}
        disabled={disabled}
        onClick={onClick}
        onMouseOver={onHover}
      >
        <Typography
          color={disabled ? 'textSecondary' : 'textPrimary'}
          className={clsx(classes.buttonText, !disabled && filled && classes.contrast)}
          variant="body2"
        >
          {value}
        </Typography>
      </IconButton>
    </div>
  );
};
