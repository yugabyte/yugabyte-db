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
