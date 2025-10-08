import { makeStyles } from '@material-ui/core';

export const usePillStyles = makeStyles((theme) => ({
  pill: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',

    width: 'fit-content',
    height: 24,
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
    '&$metadataGrey': {
      color: theme.palette.ybacolors.pillInactiveText,
      backgroundColor: theme.palette.ybacolors.pillInactiveBackground
    },
    '&$metadataWhite': {
      color: theme.palette.grey[900],
      backgroundColor: theme.palette.common.white,
      border: `1px solid ${theme.palette.grey[300]}`
    },
    '&$productOrange': {
      fontWeight: 500,
      color: theme.palette.common.white,
      backgroundColor: theme.palette.orange[500]
    },
    '&$small': {
      height: 20
    }
  },
  small: {},
  icon: {},
  ready: {},
  inProgress: {},
  warning: {},
  danger: {},
  metadataGrey: {},
  metadataWhite: {},
  productOrange: {}
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
