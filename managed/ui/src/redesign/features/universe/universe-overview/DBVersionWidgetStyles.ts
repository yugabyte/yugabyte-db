import { makeStyles } from '@material-ui/core';

export const dbVersionWidgetStyles = makeStyles((theme) => ({
  versionContainer: {
    height: '48px',
    width: '100%'
  },
  versionText: {
    color: '#44518B',
    fontSize: 18,
    fontWeight: 700
  },
  upgradeLink: {
    color: '#EF5824',
    fontSize: '12px',
    fontWeight: 500
  },
  errorIcon: {
    width: '14px',
    height: '14px',
    color: '#E73E36'
  },
  orangeText: {
    color: '#EF5824'
  },
  blueText: {
    color: '#44518B'
  }
}));
