import { makeStyles } from '@material-ui/core';

export const useOperationBannerStyles = makeStyles((theme) => ({
  banner: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    padding: theme.spacing(1),

    borderRadius: theme.shape.borderRadius,

    '&$inProgress': {
      backgroundColor: theme.palette.primary[100],
      border: `1px solid ${theme.palette.primary[200]}`,

      '& $icon': {
        color: theme.palette.primary[500]
      }
    },
    '&$warning': {
      backgroundColor: theme.palette.warning[50],
      border: `1px solid ${theme.palette.warning[100]}`,

      '& $icon': {
        color: theme.palette.warning[700]
      }
    },
    '&$success': {
      backgroundColor: theme.palette.ybacolors.success005,
      border: `1px solid ${theme.palette.success[100]}`,

      '& $icon': {
        color: theme.palette.success[500]
      }
    },
    '&$error': {
      backgroundColor: theme.palette.error[50],
      border: `1px solid ${theme.palette.error[100]}`,

      '& $icon': {
        color: theme.palette.error[500]
      }
    },

    '& .MuiTypography-subtitle1': {
      lineHeight: '16px'
    }
  },
  link: {
    marginLeft: '0 !important',

    textDecoration: 'underline',
    cursor: 'pointer',
    color: theme.palette.text.primary
  },
  icon: {
    width: 24,
    minWidth: 24,
    height: 24,
    minHeight: 24
  },
  inProgress: {},
  warning: {},
  success: {},
  error: {}
}));
