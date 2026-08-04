import { FC } from 'react';
import { Button, ButtonProps, CircularProgress, Theme, makeStyles } from '@material-ui/core';
import type { LinkProps } from 'react-router-dom';

type MUIButtonProps = ButtonProps & Partial<LinkProps>;

export interface YBButtonProps extends Omit<MUIButtonProps, 'variant' | 'color'> {
  variant?: 'primary' | 'secondary' | 'ghost' | 'gradient' | 'pill';
  showSpinner?: boolean;
  selected?: boolean; // relevant to "pill" variant only
}

const useStyles = makeStyles<Theme, Partial<YBButtonProps>>((theme) => ({
  root: {
    padding: ({ size, startIcon, endIcon }) => {
      if (startIcon || endIcon) {
        switch (size) {
          case 'large':
            return theme.spacing(0, 2);
          default:
            return theme.spacing(0, 1.4);
        }
      } else {
        switch (size) {
          case 'small':
            return theme.spacing(0, 1.4);
          case 'large':
            return theme.spacing(0, 3.5);
          default:
            return theme.spacing(0, 2);
        }
      }
    },
    background: ({ variant, disabled }) => {
      return variant === 'gradient' && !disabled
        ? 'linear-gradient(272.58deg, #7879F1 1.06%, #7879F1 33.24%, #5D5FEF 56.4%)'
        : '';
    }
  }
}));

const usePillStyles = makeStyles<Theme, Partial<YBButtonProps>>((theme) => ({
  label: {
    fontWeight: 400,
    color: theme.palette.grey[900]
  },
  text: {
    '& + &': {
      marginLeft: theme.spacing(1) // add space between sibling buttons
    },
    backgroundColor: ({ selected }) => (selected ? theme.palette.grey[300] : 'transparent'),
    '&:hover': {
      borderColor: theme.palette.grey[300],
      backgroundColor: ({ selected }) => (selected ? theme.palette.grey[300] : 'transparent')
    }
  }
}));

export const YBButton: FC<YBButtonProps> = (props) => {
  const { variant, showSpinner, ...muiProps } = props;
  const muiButtonProps: MUIButtonProps = muiProps;
  muiButtonProps.classes = { ...useStyles(props), ...props.classes };
  const pillStyles = usePillStyles(props);

  switch (variant) {
    case 'primary':
    case 'gradient':
      muiButtonProps.variant = 'contained';
      break;
    case 'secondary':
      muiButtonProps.variant = 'outlined';
      break;
    case 'ghost':
      muiButtonProps.variant = 'text';
      break;
    case 'pill':
      muiButtonProps.variant = 'text';
      muiButtonProps.classes = { ...muiButtonProps.classes, ...pillStyles };
      break;
  }

  if (showSpinner) {
    muiButtonProps.startIcon = <CircularProgress size={16} color="primary" thickness={5} />;
  }

  return <Button {...muiButtonProps} />;
};
