import { makeStyles } from '@material-ui/core';

export const useFormMainStyles = makeStyles((theme) => ({
  mainConatiner: {
    width: '100%',
    height: '100%',
    display: 'flex',
    flexDirection: 'column',
    backgroundColor: theme.palette.background.paper
  },

  formHeader: {
    marginLeft: theme.spacing(1),
    position: 'fixed',
    top: 0,
    display: 'flex',
    alignItems: 'center',
    paddingLeft: theme.spacing(2),
    height: theme.spacing(7.5), // top navbar height
    zIndex: 1030,
    '& span': {
      color: theme.palette.ybacolors.ybDarkGray1,
      marginLeft: theme.spacing(1)
    }
  },

  headerFont: {
    fontFamily: 'Rubik,Helvetica Neue,sans-serif',
    fontSize: theme.spacing(3.25),
    fontWeight: 500
  },

  headerText: {
    color: theme.palette.common.black
  },

  subHeaderFont: {
    color: '#9f9ea7',
    fontFamily: 'Rubik,Helvetica Neue,sans-serif',
    fontSize: theme.spacing(2.5),
    fontWeight: 500,
    marginLeft: theme.spacing(1),
    marginTop: theme.spacing(0.5)
  },

  selectedTab: {
    fontSize: theme.spacing(2),
    borderBottom: `3px solid ${theme.palette.orange[500]}`,
    color: theme.palette.common.black,
    fontFamily: 'Rubik,Helvetica Neue,sans-serif',
    height: '100%'
  },

  disabledTab: {
    fontSize: theme.spacing(2),
    color: theme.palette.common.black,
    height: '100%',
    opacity: 0.35,
    fontFamily: 'Rubik,Helvetica Neue,sans-serif',
    borderBottom: `3px solid ${theme.palette.ybacolors.backgroundDisabled}`
  },

  formContainer: {
    display: 'flex',
    flexDirection: 'column',
    height: '100%',
    padding: theme.spacing(0, 5),
    width: '100%',
    flexGrow: 1
  },

  formFooter: {
    display: 'flex',
    width: '100%',
    flexShrink: 1,
    padding: theme.spacing(2, 3),
    background: '#f6f6f5'
  },

  formButtons: {
    height: theme.spacing(3.75),
    borderRadius: theme.spacing(0.5)
  },

  clearRRButton: {
    '& span': {
      marginLeft: theme.spacing(0),
      fontSize: '15px',
      color: theme.palette.ybacolors.ybDarkGray
    }
  },

  universeFormButtons: {
    ['@media screen and (min-width: 500px) and (max-width: 1260px)']: {
      marginTop: theme.spacing(2)
    }
  }
}));

export const useSectionStyles = makeStyles((theme) => ({
  sectionContainer: {
    display: 'flex',
    padding: theme.spacing(5, 0),
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  sectionHeaderFont: {
    fontFamily: 'Inter',
    fontWeight: 700,
    fontSize: theme.spacing(2.25)
  },
  subsectionHeaderFont: {
    fontFamily: 'Inter',
    fontWeight: 600,
    fontSize: '15px'
  }
}));

export const useFormFieldStyles = makeStyles((theme) => ({
  itemDisabled: {
    cursor: 'not-allowed',
    opacity: 0.5
  },
  labelFont: {
    fontFamily: 'Inter',
    fontSize: '13px',
    fontWeight: theme.typography.fontWeightMedium as number
  },
  defaultTextBox: {
    maxWidth: theme.spacing(50),
    minWidth: theme.spacing(48.5)
  },
  advancedConfigLabel: {
    maxWidth: theme.spacing(28),
    minWidth: theme.spacing(27),
    zIndex: 2
  },
  advancedConfigTextBox: {
    maxWidth: theme.spacing(58.5),
    minWidth: theme.spacing(50)
  },
  instanceConfigTextBox: {
    maxWidth: theme.spacing(47.75)
  }
}));
