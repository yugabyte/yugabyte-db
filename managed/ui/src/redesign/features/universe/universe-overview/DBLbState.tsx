import { Box, makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import clsx from 'clsx';
import { YBWidget } from '../../../../components/panels';
import { ReactComponent as LoadingIcon } from '../../../assets/default-loading-circles.svg';

export const useStyles = makeStyles((theme) => ({
  versionContainer: {
    height: '48px',
    width: '100%',
    alignItems: 'center'
  },
  versionText: {
    color: '#44518B',
    fontSize: 18,
    fontWeight: 700
  },
  textMargin: {
    marginLeft: theme.spacing(0.5)
  },
  textLight: {
    fontWeight: 300
  },
  textGreen: {
    color: theme.palette.ybacolors.success
  },
  textError: {
    color: theme.palette.ybacolors.error
  },
  icon: {
    height: '16px',
    width: '16px',
    marginLeft: theme.spacing(0.5)
  },
  loadBalancedText: {
    display: 'flex'
  }
}));

interface LbState {
  isIdle: boolean;
  isEnabled: boolean;
}

interface UniverseLbStateProps {
  universeLbState: LbState;
}

export const DBLbState = ({ universeLbState }: UniverseLbStateProps) => {
  const { t } = useTranslation();
  const classes = useStyles();
  return (
    <YBWidget
      noHeader
      noMargin
      size={1}
      className={clsx(classes.versionContainer)}
      body={
        <Box
          display={'flex'}
          flexDirection={'row'}
          pt={3}
          pl={2}
          pr={2}
          width="100%"
          height={'100%'}
          justifyContent={'space-between'}
          alignItems={'center'}
        >
          <Typography variant="body1">
            <i className="fa fa-balance-scale" />
            <span className={classes.textMargin}>
              {t('universeActions.lbState.loadBalancerEnabled')}
            </span>

            {universeLbState.isEnabled ? (
              <span className={clsx(classes.textLight, classes.textGreen, classes.textMargin)}>
                <i className={'fa fa-check'} />
              </span>
            ) : (
              <span className={clsx(classes.textLight, classes.textError, classes.textMargin)}>
                <i className={'fa fa-exclamation-triangle'} />
              </span>
            )}
          </Typography>
          {universeLbState.isEnabled && (
            <Typography variant="body1" className={classes.loadBalancedText}>
              <span className={classes.textMargin}>
                {universeLbState.isIdle
                  ? t('universeActions.lbState.loadIsBalanced')
                  : t('universeActions.lbState.loadBalanceInProgress')}
              </span>
              {universeLbState.isIdle ? (
                <span className={clsx(classes.textLight, classes.textGreen, classes.textMargin)}>
                  <i className={'fa fa-check'} />
                </span>
              ) : (
                <span>
                  <LoadingIcon className={clsx(classes.icon)} />
                </span>
              )}
            </Typography>
          )}
        </Box>
      }
    />
  );
};
