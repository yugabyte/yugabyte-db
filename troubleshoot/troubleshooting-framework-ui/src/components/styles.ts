import { makeStyles } from '@material-ui/core';

export const useHelperStyles = makeStyles((theme) => ({
  recommendation: {
    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: theme.shape.borderRadius,
    padding: theme.spacing(2, 2.5),
    position: 'relative',
    marginBottom: theme.spacing(3)
  },
  inactiveIssue: {
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
  troubleshootBox: {
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
  troubleshootHeaderDescription: {
    color: '#0B1117',
    fontSize: 13,
    fontFamily: "Inter",
    fontWeight: 400,
    lineHeight: 1.25
  },
  troubleshootTitle: {
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
  },
  recommendationAdvice: {
    display: 'flex',
    alignItems: 'center',
    marginTop: theme.spacing(1.5)
  },
  learnMoreImage: {
    width: theme.spacing(2),
    height: theme.spacing(2),
    marginTop: '-6px',
    marginRight: theme.spacing(1)
  },
   learnRecommendationSuggestions: {
    fontSize: '13px',
    fontFamily: 'Inter',
    fontWeight: 400,
    padding: 0,
    color: '#EF5824',
    textDecoration: 'underline',
    '&:hover': {
      textDecoration: 'underline',
      cursor: 'pointer',
    }
  },
   anomalyTitle: {
    fontSize: '13px',
    fontFamily: 'Inter',
    fontWeight: 600,
  },
   queryBox: {
    border: '1px solid #B7C3CB',
    borderRadius: '8px',
    padding: '3px',
    backgroundColor: '#FFFFFF'
  },
  redirectLinkText: {
    textDecoration: 'underline',
    color: '#0B1117'
  },
  bulletImage: {
    width: '15px',
    height: '12px',
  },
  guidelineBox: {
    border: '1px solid #e5e5e9',
    paddingTop: '15px',
    paddingLeft: '25px',
    paddingBottom: '50px',
    paddingRight: '25px',
    // borderRadius: '8px',
    width: '93.5%'
  },
  flexRow: {
    display: 'flex',
    flexDirection: 'row'
  },
  flexColumn: {
    display: 'flex',
    flexDirection: 'column'
  },
  metaDataLabel: {
    alignItems: 'baseline'
  },
  incidentStatus: {
    marginLeft: theme.spacing(0.5)
  },
  arrowIcon: {
    float: 'right',
    marginTop: '-30px'
  },
  smallBold: {
    fontWeight: 700,
    fontSize: 15,
    fontFamily: 'Inter',
  },
  smallNormal: {
    fontWeight: 600,
    fontSize: 11.5,
    color: '#555',
    fontFamily: 'Inter',
  },
  mediumNormal: {
    fontWeight: 400,
     fontSize: 15,
    fontFamily: 'Inter',
  },
  largeBold: {
    fontWeight: 600,
    fontSize: 18,
    fontFamily: 'Inter',
  },
  secondaryDashboard: {
    background: '#F9FAFC',
    border: `2px solid #E9EEF2`,
    borderRadius: '16px',
    padding: theme.spacing(2, 2.5),
    marginBottom: theme.spacing(3)
  },
  metricGroupItems: {
    display: 'flex',
    flexDirection: 'row',
    overflow: 'hidden',
    flexWrap: 'wrap'
  }
}));
