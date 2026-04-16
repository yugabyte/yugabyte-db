import { makeStyles } from '@material-ui/core';

export const useStyles = makeStyles((theme) => ({
  recommendation: {
    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: theme.shape.borderRadius,
    padding: theme.spacing(2, 2.5),
    position: 'relative',
    marginBottom: theme.spacing(3)
  },
  inactiveRecommendation: {
    opacity: 0.5
  },
  itemHeader: {
    cursor: 'pointer'
  },
  typeTag: {
    fontWeight: 500,
    fontSize: 10,
    lineHeight: '16px',
    padding: theme.spacing(0.25, 0.75),
    width: 'fit-content',
    borderRadius: theme.spacing(0.5),
    marginRight: theme.spacing(3)
  },
  infoSection: {
    minHeight: '100px',
    marginBottom: theme.spacing(2)
  },
  recommendationBox: {
    marginTop: theme.spacing(3),
    marginBottom: theme.spacing(3),
    border: `1px solid ${theme.palette.grey[200]}`,
    background: 'rgba(243, 246, 249, 0.5)',
    borderRadius: theme.shape.borderRadius,
    padding: theme.spacing(2.5)
  },
  closeIcon: {
    position: 'absolute',
    right: theme.spacing(2),
    bottom: theme.spacing(2),
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),
    cursor: 'pointer'
  },
  boldTargetName: {
    margin: theme.spacing(0, 0.25)
  },
  codeTag: {
    color: theme.palette.grey[700],
    border: `1px solid ${theme.palette.grey[400]}`,
    padding: theme.spacing(0.25, 0.5),
    borderRadius: 5,
    background: theme.palette.common.white,
    fontFamily: 'Inter'
  },
  disabled: {
    cursor: 'not-allowed',
    color: '#999999'
  },
  resolvedTag: {
    opacity: 0.5
  },
  strikeThroughText: {
    '& > span': {
      textDecorationLine: 'line-through',
      opacity: 0.5
    }
  },
  recommendationHeaderDescription: {
    color: '#0B1117',
    fontSize: 13,
    fontFamily: "Inter",
    fontWeight: 400,
    lineHeight: 1.25
  },
  recommendationTitle: {
     border: '1px solid #D6ECEC',
     padding: '3px',
     background: '#EBF6F6',
     fontWeight: 500,
     fontSize: 10,
     lineHeight: '16px',
     marginRight: theme.spacing(3),
     borderRadius: theme.spacing(0.5)
  },
  tagGreen: {
    color: '#206263'
  },
  tagBlue: {
    color: '$262666'
  }
}));
