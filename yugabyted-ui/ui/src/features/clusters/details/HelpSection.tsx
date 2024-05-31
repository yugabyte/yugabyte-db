import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Typography, Paper, Link, makeStyles, Box, Grid } from '@material-ui/core';
import { YBButton, YBTooltip } from '@app/components';
// import { ConnectModalTabEnum } from '@app/features/clusters/connect/ConnectModal';
import { OPEN_CLOUD_SHELL_MODAL, OPEN_ALLOW_LIST_SIDE_PANEL } from '@app/helpers';
import CloudShellIcon from '@app/assets/cloud-shell.svg';
import NetworkIcon from '@app/assets/network-access.svg';
import DocumentationIcon from '@app/assets/documentation.svg';
import TimesWhiteIcon from '@app/assets/times-white-bg.svg';
import clsx from 'clsx';

const useStyles = makeStyles((theme) => ({
  sectionHeader: {
    fontWeight: 'bold'
  },
  sectionHeaderMargin: {
    marginLeft: theme.spacing(1.5)
  },
  sectionSubText: {
    marginTop: theme.spacing(0.6)
  },
  newClusterSection: {
    margin: theme.spacing(1.5, 0),
    padding: theme.spacing(3),
    display: 'flex',
    justifyContent: 'flex-start'
  },
  columnSeparator: {
    width: '1px',
    height: '35%',
    backgroundColor: theme.palette.grey[500],
    margin: '0 auto',
    opacity: 0.3
  },
  wordSeparator: {
    margin: 'auto'
  },
  closeIcon: {
    marginLeft: 'auto',
    marginTop: theme.spacing(-1),
    marginRight: theme.spacing(-1),
    width: 32,
    height: 32,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    borderRadius: '50%',
    cursor: 'pointer'
  },
  centerText: {
    textAlign: 'center'
  },
  numberCircle: {
    borderRadius: '50%',
    width: 22,
    height: 22,
    border: ' 1px solid',
    borderColor: theme.palette.grey[900],
    padding: theme.spacing(0.5, 1)
  },
  itemSection: {
    marginRight: theme.spacing(8),
    display: 'flex',
    flexDirection: 'column',
    justifyContent: 'space-between',
    '& > *': {
      marginTop: theme.spacing(1)
    }
  },
  sectionListHeader: {
    marginBottom: theme.spacing(1)
  },
  sectionIcon: {
    transform: 'scale(0.8)'
  },
  marginTop: {
    marginTop: theme.spacing(2.5)
  }
}));

const NETWORK_ACCESS_DOCS_LINK =
  'https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-secure-clusters/add-connections/';

interface HelpSectionProps {
  dispatch: (value: Record<string, string>) => void;
  openModal: (index: number) => void;
  onClose: () => void;
  actionsDisabled?: boolean;
  tooltip?: string;
}

export const HelpSection: FC<HelpSectionProps> = ({ dispatch, tooltip, actionsDisabled, onClose }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Paper className={classes.newClusterSection}>
      <Box width={300} textAlign="center">
        <Typography variant="h5" className={classes.sectionHeader}>
          {t('clusterDetail.help.exploreCluster')}
        </Typography>
        <Typography variant="body2" color="textSecondary" className={classes.sectionSubText}>
          {t('clusterDetail.help.networkAccessSubtitle')}
        </Typography>
        <YBTooltip title={tooltip ?? ''} placement="top">
          <Box mt={3}>
            <YBButton
              variant="gradient"
              size="large"
              disabled={actionsDisabled}
              startIcon={<CloudShellIcon />}
              onClick={() => dispatch({ type: OPEN_CLOUD_SHELL_MODAL })}
              data-testid="LaunchCloudShellBtn"
            >
              {t('connectModal.launchCloudShell')}
            </YBButton>
          </Box>
        </YBTooltip>
        <Typography
          variant="subtitle1"
          color="textSecondary"
          className={clsx(classes.sectionSubText, classes.marginTop)}
        >
          {t('clusterDetail.help.interactWithCluster')}
        </Typography>
      </Box>
      <Box width={20} textAlign="center" mx={5} height={168} display="flex" flexDirection="column">
        <div className={classes.columnSeparator} />
        <span className={classes.wordSeparator}>{t('connectModal.separatorOr')}</span>
        <div className={classes.columnSeparator} />
      </Box>
      <Box pl={5}>
        <Typography variant="h5" className={clsx(classes.sectionHeader, classes.sectionHeaderMargin)}>
          {t('clusterDetail.help.connectToCluster')}
        </Typography>

        <Box display="flex" mt={2}>
          <div className={classes.itemSection}>
            <Grid container className={classes.sectionListHeader}>
              <Grid item xs={2} md={2} className={classes.centerText}>
                <Typography variant="subtitle1">
                  <span className={classes.numberCircle}>{'1'}</span>
                </Typography>
              </Grid>
              <Grid item xs={10} md={10}>
                <Typography variant="body2">{t('clusterDetail.help.networkAccessSection')}</Typography>
              </Grid>
            </Grid>
            <Link onClick={() => dispatch({ type: OPEN_ALLOW_LIST_SIDE_PANEL })}>
              <Grid container alignItems="center">
                <Grid item xs={2} md={2} className={classes.centerText}>
                  <NetworkIcon className={classes.sectionIcon} />
                </Grid>
                <Grid item xs={10} md={10}>
                  {t('network.addIpAllowlist')}
                </Grid>
              </Grid>
            </Link>
            <Link onClick={() => window.open(NETWORK_ACCESS_DOCS_LINK)}>
              <Grid container>
                <Grid item xs={2} md={2} className={classes.centerText}>
                  <DocumentationIcon className={classes.sectionIcon} />
                </Grid>
                <Grid item xs={10} md={10}>
                  {t('clusterDetail.help.networkAccessDocs')}
                </Grid>
              </Grid>
            </Link>
          </div>
          <div className={classes.itemSection}>
            <Grid container className={classes.sectionListHeader}>
              <Grid item xs={2} md={2} className={classes.centerText}>
                <Typography variant="subtitle1">
                  <span className={classes.numberCircle}>{'2'}</span>
                </Typography>
              </Grid>
              <Grid item xs={10} md={10}>
                <Typography variant="body2">{t('clusterDetail.connect')}</Typography>
              </Grid>
            </Grid>
            <Grid container>
              <Grid item xs={2} md={2} className={classes.centerText}>
                <Typography variant="body2" color={'textSecondary'}>
                  {t('connectModal.separatorOr')}
                </Typography>
              </Grid>
            </Grid>
          </div>
        </Box>
      </Box>
      <div className={classes.closeIcon} onClick={onClose}>
        <TimesWhiteIcon />
      </div>
    </Paper>
  );
};
