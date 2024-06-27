import { makeStyles } from '@material-ui/core';

export const exportLogStyles = makeStyles((theme) => ({
  mainTitle: {
    fontWeight: 700,
    fontSize: 22,
    color: '#151838'
  },
  emptyContainer: {
    display: 'flex',
    flexDirection: 'column',
    height: '368px',
    width: '100%',
    borderRadius: '8px',
    backgroundColor: '#FFFFFF',
    border: '1px dotted #C8C7CE',
    padding: theme.spacing(8, 2, 2, 2),
    alignItems: 'center',
    justifyContent: 'space-between'
  },
  configNameContainer: {
    display: 'flex',
    width: '384px',
    marginTop: '4px'
  },
  mainFieldContainer: {
    display: 'flex',
    flexDirection: 'column',
    width: '100%',
    paddingTop: theme.spacing(3),
    borderTop: '1px solid #E5E5E9',
    marginTop: theme.spacing(4)
  },
  exportToTitle: {
    fontWeight: 700,
    fontSize: 14,
    color: '#000000',
    display: 'block',
    marginBottom: theme.spacing(1)
  },
  exportListContainer: {
    display: 'flex',
    flexDirection: 'column',
    height: '560px',
    width: '100%',
    padding: theme.spacing(2, 2, 4, 2),
    backgroundColor: '#FFFFFF',
    border: '1px solid #E5E5E9'
  },
  dataDogmenuItem: {
    height: 56,
    dispaly: 'flex',
    flexDirection: 'column',
    alignItems: 'flex-start',
    rowGap: theme.spacing(0.5)
  },
  gcpJSONUploader: {
    display: 'flex',
    flexDirection: 'column',
    width: '100%',
    padding: theme.spacing(4),
    height: 'auto',
    alignItems: 'center',
    textAlign: 'center'
  }
}));
