import { makeStyles } from '@material-ui/core';

export const useGettingStartedStyles = makeStyles((theme) => ({
  root: {
    maxWidth: 1200,
    minHeight: 425,
    padding: theme.spacing(0, 4),
    margin: theme.spacing(2, 'auto', 0, 'auto'),
    backgroundColor: theme.palette.background.paper,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  headerTitle: {
    display: 'flex',
    alignItems: 'center',
    padding: theme.spacing(4, 2)
  },
  getStartedTitle: {
    padding: theme.spacing(7, 2, 5, 2)
  },
  getStartedBlock: {
    display: 'flex',
    padding: theme.spacing(0, 2, 5, 2)
  },
  features: {
    borderLeft: `1px solid ${theme.palette.grey[200]}`,
    padding: theme.spacing(1, 1, 1, 5)
  },
  featureItem: {
    display: 'flex',
    alignItems: 'flex-start',
    marginBottom: theme.spacing(2.5),
    '&:last-child': {
      marginBottom: 0
    }
  },
  iconCheck: {
    marginRight: theme.spacing(3),
    color: theme.palette.grey[600]
  },
  tile: {
    width: '100%',
    height: 165,
    display: 'flex',
    flexDirection: 'column',
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,
    padding: theme.spacing(3, 4),
    backgroundColor: theme.palette.background.default,
    marginRight: theme.spacing(4),
    '&:last-child': {
      marginRight: 0
    }
  },
  link: {
    display: 'flex',
    alignItems: 'center',
    marginTop: 'auto'
  },
  users: {
    color: theme.palette.grey[600],
    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.grey[200]}`,
    padding: theme.spacing(2),
    marginTop: theme.spacing(2),
    display: 'flex',
    alignItems: 'center'
  }
}));
