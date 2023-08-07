import { makeStyles } from '@material-ui/core';

export const dbSettingStyles = makeStyles((theme) => ({
  mainContainer: {
    display: 'flex',
    flexDirection: 'column',
    backgroundColor: theme.palette.ybacolors.backgroundGrayLightest,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1),
    padding: theme.spacing(2)
  },
  rotatePwdContainer: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    background: '#FFFFFF',
    borderRadius: theme.spacing(1),
    padding: theme.spacing(0.5, 1.5)
  },
  errorNote: {
    color: '#a94442'
  }
}));
