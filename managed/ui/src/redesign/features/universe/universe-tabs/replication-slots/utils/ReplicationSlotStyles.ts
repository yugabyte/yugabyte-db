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
  },
  slotDetailContainer: {
    width: '100%',
    height: '100%',
    display: 'flex',
    flexDirection: 'column',
    backgroundColor: '#F7F7F7'
  },
  slotDetailHeader: {
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
    },
    fontFamily: 'Rubik',
    flexDirection: 'row'
  },
  slotMainDetails: {
    backgroundColor: theme.palette.background.paper,
    width: '100%',
    height: '48px',
    display: 'flex',
    border: '1px solid #DDE1E6',
    borderRadius: '8px',
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: theme.spacing(0, 2.5)
  },
  slotMetricContainer: {
    display: 'flex',
    width: '100%',
    minHeight: '200px',
    borderRadius: '10px',
    backgroundColor: theme.palette.background.paper,
    border: '1px solid #DDE1E6',
    padding: theme.spacing(4),
    flexDirection: 'column'
  },
  metricHiglights: {
    display: 'flex',
    width: '100%',
    height: '110px',
    flexDirection: 'row',
    borderRadius: '8px',
    alignItems: 'center',
    backgroundColor: theme.palette.background.paper,
    border: '1px solid #DDE1E6',
    padding: theme.spacing(0, 4),
    justifyContent: 'space-between'
  },
  metricWidget: {
    border: '1px solid #E5E5E9',
    borderRadius: '8px',
    boxShadow: '0px !important',
    height: '400px'
  },
  slotUniverseTitle: {
    color: '#151838',
    fontFamily: 'Rubik',
    fontSize: '26px',
    fontWeight: 500,
    marginLeft: theme.spacing(1)
  },
  slotReplicationTitle: {
    color: '#232329',
    opacity: 0.4,
    fontFamily: 'Rubik',
    fontWeight: 500,
    fontSize: '21px',
    marginLeft: theme.spacing(1)
  },
  metricHiglightNumber: {
    fontWeight: 500,
    fontSize: '24px',
    color: '#000000'
  },
  metricLightNumber: {
    fontWeight: 400,
    fontSize: '12px',
    color: '#000000'
  },
  metricHighlightsTitle: {
    fontWeight: 400,
    fontSize: '14px',
    color: '#000000'
  },
  metricDivider: {
    height: '1px',
    backgroundColor: '#E5E5E9',
    width: '50px'
  },
  successStatus: {
    color: '#37A24F',
    fontWeight: 600,
    fontSize: '16px'
  },
  errorStatus: {
    color: '#E73E36',
    fontWeight: 600,
    fontSize: '16px'
  },
  expiryStatus: {
    color: '#4e5f6d',
    fontWeight: 600,
    fontSize: '16px'
  },
  interactiveIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  }
}));
