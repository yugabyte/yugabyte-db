import { makeStyles } from '@material-ui/core';

export const usePillStyles = makeStyles((theme) => ({
  pill: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',

    width: 'fit-content',
    padding: `${theme.spacing(0.5)}px ${theme.spacing(0.75)}px`,

    fontSize: '11.5px',
    borderRadius: '6px',
    backgroundColor: theme.palette.ybacolors.pillInactiveBackground,

    '&$ready': {
      color: theme.palette.ybacolors.pillReadyText,
      backgroundColor: theme.palette.ybacolors.pillReadyBackground,
      '& $icon': {
        color: theme.palette.ybacolors.pillReadyIcon
      }
    },
    '&$inProgress': {
      color: theme.palette.ybacolors.pillInProgressText,
      backgroundColor: theme.palette.ybacolors.pillInProgressBackground,
      '& $icon': {
        color: theme.palette.ybacolors.pillInProgressIcon
      }
    },
    '&$warning': {
      color: theme.palette.ybacolors.pillWarningText,
      backgroundColor: theme.palette.ybacolors.pillWarningBackground,
      '& $icon': {
        color: theme.palette.ybacolors.pillWarningIcon
      }
    },
    '&$danger': {
      color: theme.palette.ybacolors.pillDangerText,
      backgroundColor: theme.palette.ybacolors.pillDangerBackground,
      '& $icon': {
        color: theme.palette.ybacolors.pillDangerIcon
      }
    },
    '&$inactive': {
      color: theme.palette.ybacolors.pillInactiveText,
      backgroundColor: theme.palette.ybacolors.pillInactiveBackground
    }
  },
  icon: {},
  ready: {},
  inProgress: {},
  warning: {},
  danger: {},
  inactive: {}
}));

export const useIconStyles = makeStyles(() => ({
  interactiveIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  }
}));

export const useAnchorStyles = makeStyles(() => ({
  iconAnchor: {
    display: 'flex',
    alignItems: 'center'
  }
}));
