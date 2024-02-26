import { makeStyles } from '@material-ui/core';

export const replicationSlotStyles = makeStyles((theme) => ({
  emptyContainer: {
    height: '283px',
    width: '100%',
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: theme.palette.background.paper
  },
  emptyContainerTitle: {
    fontSize: '15px',
    fontWeight: 600,
    lineHeight: '20px',
    color: '#555555'
  },
  emptyContainerSubtitle: {
    color: '#4E5F6D'
  },
  emptyContainerLink: {
    color: '#2B59C3',
    fontSize: '11.5px',
    fontWeight: 400,
    lineHeight: '16px'
  },
  slotLengthText: {
    fontWeight: 600,
    fontSize: 18,
    lineHeight: '22px'
  }
}));
