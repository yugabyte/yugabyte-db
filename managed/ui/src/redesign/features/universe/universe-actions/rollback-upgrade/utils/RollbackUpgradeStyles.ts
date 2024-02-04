import { makeStyles } from '@material-ui/core';

export const dbUpgradeFormStyles = makeStyles((theme) => ({
  mainContainer: {
    display: 'flex',
    flexDirection: 'column',
    justifyContent: 'space-between',
    width: '100%',
    height: '100%',
    padding: theme.spacing(2, 1, 1, 1)
  },
  xclusterBanner: {
    display: 'flex',
    flexDirection: 'row',
    width: '100%',
    padding: theme.spacing(2),
    backgroundColor: '#FFEEC8',
    marginTop: theme.spacing(2),
    borderRadius: '8px'
  },
  additionalStepContainer: {
    display: 'flex',
    width: '100%',
    backgroundColor: '#F0F4F7',
    border: '1px solid #E5E5E9',
    borderRadius: '8px',
    padding: theme.spacing(1, 2),
    marginTop: theme.spacing(1)
  },
  upgradeDetailsContainer: {
    width: '100%',
    display: 'flex',
    flexDirection: 'column',
    padding: theme.spacing(2),
    marginTop: theme.spacing(2),
    border: '1px solid #E5E5E9',
    borderRadius: '8px'
  },
  greyFooter: {
    width: '100%',
    display: 'flex',
    flexDirection: 'row',
    backgroundColor: '#F0F4F7',
    border: '1px solid #E5E5E9',
    borderRadius: '8px',
    padding: theme.spacing(2),
    alignItems: 'center',
    justifyContent: 'center'
  },
  releaseTypebadge: {
    marginLeft: theme.spacing(1),
    borderRadius: '4px',
    border: '1px solid #E9EEF2',
    backgroundColor: '#F0F4F7',
    color: 'grey',
    padding: '2px 6px',
    fontSize: '10px'
  }
}));

export const preFinalizeStateStyles = makeStyles((theme) => ({
  bannerContainer: {
    display: 'flex',
    flexDirection: 'row',
    width: '100%',
    padding: theme.spacing(2),
    backgroundColor: '#FFEEC8'
  },
  modalContainer: {
    display: 'flex',
    flexDirection: 'column',
    width: '100%',
    height: '100%',
    padding: theme.spacing(2, 1)
  }
}));

export const rollBackStyles = makeStyles((theme) => ({
  bannerContainer: {
    display: 'flex',
    flexDirection: 'column',
    width: '100%',
    height: '110px',
    backgroundColor: '#FDE2E2',
    padding: theme.spacing(2)
  }
}));
