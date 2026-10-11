import { makeStyles } from '@material-ui/core';

export const dbVersionWidgetStyles = makeStyles((theme) => ({
  versionContainer: {
    height: '48px',
    width: '100%'
  },
  upgradeLink: {
    color: '#EF5824',
    fontSize: '12px',
    fontWeight: 500
  },
  upgradeLinkDisabled: {
    cursor: 'not-allowed',
    pointerEvents: 'none'
  },
  upgradeAvailableLinkTarget: {
    display: 'inline-flex',
    alignItems: 'center'
  },
  upgradeAvailableLinkTargetDisabled: {
    cursor: 'not-allowed'
  },
  text: {
    color: theme.palette.ybacolors.primary4
  },
  upgradeAvailableLinkContainer: {
    display: 'flex',
    alignItems: 'center'
  },
  upgradeStateContainer: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  }
}));
