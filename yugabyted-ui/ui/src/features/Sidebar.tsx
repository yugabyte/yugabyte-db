import React, { FC, useState } from 'react';
import clsx from 'clsx';
import { Link, NavLink, NavLinkProps, useRouteMatch } from 'react-router-dom';
import { makeStyles, Typography, Link as MUILink, Box } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { browserStorage } from '@app/helpers';

import YBLogoFull from '@app/assets/yugabyteDB-logo.svg';
import YBLogo from '@app/assets/yb-logo.svg';
import RocketIcon from '@app/assets/rocket.svg';
import SettingsIcon from '@app/assets/cog.svg';
import DbSecurityIcon from '@app/assets/database-security.svg';
import MetricsIcon from '@app/assets/stats.svg';
import DoubleArrowIcon from '@app/assets/double-arrow-left.svg';
import AlertsIcon from '@app/assets/bell.svg';
import DatabaseIcon from '@app/assets/database.svg'
import { themeVariables } from '@app/theme/variables';
import { useAlerts } from './clusters/details/alerts/alerts';

// Global state for setting and getting new alert flag that can be used on alerts list page
// export const useAlertGlobalValue = createGlobalState<boolean>(() => false);

const useStyles = makeStyles((theme) => ({
  filler: {
    flexShrink: 0,
    width: themeVariables.sidebarWidthMax,
    transition: theme.transitions.create('width')
  },
  sidebar: {
    position: 'fixed',
    height: '100%',
    display: 'flex',
    flexDirection: 'column',
    width: themeVariables.sidebarWidthMax,
    transition: theme.transitions.create('width'),
    backgroundColor: theme.palette.background.paper,
    borderRight: `1px solid ${theme.palette.grey[200]}`,
    boxShadow: `inset -1px 0 0 0 ${theme.palette.grey[200]}`,
    zIndex: theme.zIndex.drawer
  },
  collapsed: {
    width: themeVariables.sidebarWidthMin
  },
  linkRow: {
    display: 'flex',
    alignItems: 'center',
    // override browser styles for <a>
    '&:link, &:focus, &:active, &:visited': {
      textDecoration: 'none',
      color: theme.palette.text.secondary
    }
  },
  logoIcon: {
    width: "100%",
    height: themeVariables.sidebarWidthMin,
    minHeight: themeVariables.sidebarWidthMin,
    padding: theme.spacing(2.5),
    marginRight: theme.spacing(3)
  },
  logoIconCollapsed: {
    padding: theme.spacing(2),
    marginRight: 0
  },
  claimShirtIcon: {
    display: 'flex',
    alignItems: 'center',
    padding: theme.spacing(4, 2),
    '&:before': {
      content: '"\uD83D\uDC4B"', // "waving hand" unicode symbol
      fontSize: 24,
      marginRight: theme.spacing(1.5)
    }
  },

  link: {
    height: 56,
    display: 'flex',
    alignItems: 'center',
    '&:hover': {
      boxShadow: `inset 4px 0px 0px 0px ${theme.palette.secondary[300]}`
    },
    // override browser styles for <a>
    '&:link, &:focus, &:active, &:visited': {
      textDecoration: 'none',
      color: theme.palette.text.primary
    }
  },
  linkActive: {
    boxShadow: `inset 4px 0px 0px 0px ${theme.palette.secondary[900]}`,
    '&:hover': {
      boxShadow: `inset 4px 0px 0px 0px ${theme.palette.secondary[900]}`
    }
  },
  icon: {
    minWidth: 24,
    color: theme.palette.secondary[800],
    marginLeft: theme.spacing(2.5),
    marginRight: theme.spacing(3)
  },
  footer: {
    display: 'flex',
    alignItems: 'center',
    marginTop: 'auto',
    padding: theme.spacing(2, 0)
  },
  footerSmall: {
    flexDirection: 'column'
  },
  statusIcon: {
    margin: theme.spacing(0, 2)
  },
  expandIcon: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    borderRadius: 4,
    border: `1px solid ${theme.palette.grey[300]}`,
    padding: theme.spacing(0.5),
    marginLeft: 'auto',
    marginRight: theme.spacing(2),
    cursor: 'pointer',
    '&:hover': {
      borderColor: theme.palette.grey[400]
    }
  },
  rotateExpandIcon: {
    transform: 'rotate(180deg)',
    marginTop: theme.spacing(3),
    marginLeft: 'unset',
    marginRight: 'unset'
  },
  fadeOut: {
    transition: theme.transitions.create('opacity'),
    opacity: 0
  },
  newAlerts: {
    borderRadius: '50%',
    background: theme.palette.error[500],
    width: 9,
    position: 'absolute',
    right: theme.spacing(3),
    top: theme.spacing(-0.3),
    height: 9
  }
}));

interface NavLinkWithDisableProps extends NavLinkProps {
  disabled?: boolean;
  className?: string;
}

const YB_WIN_TSHIRT_PAGE = 'https://www.yugabyte.com/community-rewards/'

const NavLinkWithDisable: FC<NavLinkWithDisableProps> = ({ disabled, ...rest }) => {
  return disabled ? <span className={rest.className}>{rest.children}</span> : <NavLink {...rest} />;
};

export const Sidebar: FC<{ projectId: string }> = ({ projectId }) => {
  const classes = useStyles();
  const {
    url,
    params: {}
  } = useRouteMatch<App.RouteParams>();
  const [isCollapsed, setCollapsed] = useState(browserStorage.sidebarCollapsed);
  const { t } = useTranslation();

  const toggleWidth = () => {
    const newValue = !isCollapsed;
    setCollapsed(newValue);
    browserStorage.sidebarCollapsed = newValue;
  };

  const { data: alerts } = useAlerts(true);

  // const { data: runtimeConfig } = useRuntimeConfig();
 
  //   //Alerts Policy - Hide Alerting bell icon on sidebar
  //   const ALERTS_FLAG_PATH = 'ybcloud.conf.alerts.enabled';
  //   const { data: alertsFeatGlobal } = useGetAppConfigQuery({
  //     paths: [ALERTS_FLAG_PATH],
  //     scopeType: GetAppConfigScopeTypeEnum.Global
  //   });
  //   const { data: alertsFeatAccount } = useGetAppConfigQuery({
  //     paths: [ALERTS_FLAG_PATH],
  //     scopeType: GetAppConfigScopeTypeEnum.Account,
  //     scopeId: accountId
  //   });

  // const alertsEnabledGlobal = !!alertsFeatGlobal?.data?.find(
  //   (item) => item.path === ALERTS_FLAG_PATH && item.value === 'true'
  // );
  // const alertsEnabledAccount = !!alertsFeatAccount?.data?.find(
  //   (item) => item.path === ALERTS_FLAG_PATH && item.value === 'true'
  // );
  // const isAlertsEnabled = alertsEnabledAccount || (alertsEnabledAccount && alertsEnabledGlobal);
  const isAlertsEnabled = true;

  //Alerts Policy feature - code can be removed in future once feature is stable

  // const isDisabled = accountId === undefined || accountId === '' || accountId === 'accounts';
  const isDisabled = false;

  // if length of alerts data greater 0 then there is new alert and show red dot
  // const isNewAlertFlag = newAlertCheck ? newAlertCheck?.data?.length > 0 : false;

  // useEffect(() => {
  //   setIsNewAlert(isNewAlertFlag);
  // }, [isNewAlertFlag, setIsNewAlert]);

  return (
    <>
      <div className={clsx(classes.filler, isCollapsed && classes.collapsed)} />
      <div className={clsx(classes.sidebar, isCollapsed && classes.collapsed)}>
        <Link to="/" className={classes.linkRow}>
          {!isCollapsed ?
            <YBLogoFull className={classes.logoIcon} />
            :
            <YBLogo className={clsx(classes.logoIcon, classes.logoIconCollapsed)} />
          }
        </Link>
        {/* {!runtimeConfig?.PLGOnboardingPhase1 && (
          <NavLinkWithDisable
            disabled={isDisabled}
            to={`/welcome`}
            className={classes.link}
            activeClassName={classes.linkActive}
            data-testid="SidebarLinkWelcome"
          >
            <RocketIcon className={classes.icon} />
            <Typography variant="body2" noWrap className={clsx(isCollapsed && classes.fadeOut)}>
              {t('welcome.gettingStarted')}
            </Typography>
          </NavLinkWithDisable>
        )} */}
        <NavLinkWithDisable
          disabled={isDisabled}
          to={`/`}
          isActive={(_, location) => /^\/$/.test(location.pathname)}
          className={classes.link}
          activeClassName={classes.linkActive}
          data-testid="SidebarLinkCluster"
        >
          <RocketIcon className={classes.icon} />
          <Typography variant="body2" noWrap className={clsx(isCollapsed && classes.fadeOut)}>
            {t('common.cluster')}
          </Typography>
        </NavLinkWithDisable>
        <NavLinkWithDisable
          disabled={isDisabled}
          to={`/databases/tabYsql`}
          isActive={(_, location) => /^\/databases/.test(location.pathname)}
          className={classes.link}
          activeClassName={classes.linkActive}
          data-testid="SidebarLinkDatabases"
        >
          <DatabaseIcon className={classes.icon} />
          <Typography variant="body2" noWrap className={clsx(isCollapsed && classes.fadeOut)}>
            {t('common.databases')}
          </Typography>
        </NavLinkWithDisable>
        <NavLinkWithDisable
          disabled={isDisabled}
          to={`/performance/metrics`}
          isActive={(_, location) => /^\/performance/.test(location.pathname)}
          className={classes.link}
          activeClassName={classes.linkActive}
          data-testid="SidebarLinkPerformance"
        >
          <MetricsIcon className={classes.icon} />
          <Typography variant="body2" noWrap className={clsx(isCollapsed && classes.fadeOut)}>
            {t('clusterDetail.tabPerformance')}
          </Typography>
        </NavLinkWithDisable>
        {isAlertsEnabled && (
          <NavLinkWithDisable
            disabled={isDisabled}
            to={`/alerts/tabNotifications`}
            isActive={(_, location) => /^\/alerts/.test(location.pathname)}
            className={classes.link}
            activeClassName={classes.linkActive}
            data-testid="alertsPageNav"
          >
            <Box position="relative">
              {alerts.length > 0 && <Box className={classes.newAlerts}></Box>}
              <AlertsIcon className={classes.icon} />
            </Box>
            <Typography variant="body2" noWrap className={clsx(isCollapsed && classes.fadeOut)}>
              {t('common.alerts')}
            </Typography>
          </NavLinkWithDisable>
        )}
        <div hidden>
          <NavLinkWithDisable
            disabled={isDisabled}
            to={`${url}/p/${projectId}/analytics`}
            isActive={(_, location) => /\/a\/.+\/p\/.+\/analytics/.test(location.pathname)}
            className={classes.link}
            activeClassName={classes.linkActive}
            data-testid="SidebarLinkAnalytics"
          >
            <MetricsIcon className={classes.icon} />
            <Typography variant="body2" noWrap className={clsx(isCollapsed && classes.fadeOut)}>
              {t('common.analytics')}
            </Typography>
          </NavLinkWithDisable>
        </div>
        <div hidden>
          <NavLinkWithDisable
            disabled={isDisabled}
            to={`${url}/dbsecurity`}
            className={classes.link}
            activeClassName={classes.linkActive}
            data-testid="SidebarLinkDBSecurity"
          >
            <DbSecurityIcon className={classes.icon} />
            <Typography variant="body2" noWrap className={clsx(isCollapsed && classes.fadeOut)}>
              {t('common.dbSecurity')}
            </Typography>
          </NavLinkWithDisable>
        </div>
        <NavLinkWithDisable
          disabled={isDisabled}
          to={`/debug`}
          isActive={(_, location) => /^\/debug/.test(location.pathname)}
          className={classes.link}
          activeClassName={classes.linkActive}
          data-testid="SidebarLinkDebug"
        >
          <SettingsIcon className={classes.icon} />
          <Typography variant="body2" noWrap className={clsx(isCollapsed && classes.fadeOut)}>
            {t('common.debug')}
          </Typography>
        </NavLinkWithDisable>

        <div className={clsx(classes.footer, isCollapsed && classes.footerSmall)}>
          <MUILink className={classes.linkRow} href={YB_WIN_TSHIRT_PAGE} target="_blank" data-testid="systemStatus">
            {!isCollapsed && (
              <Typography variant="body2" noWrap color="textSecondary" className={classes.claimShirtIcon}>
                {t('common.claimTshirt')}
              </Typography>
            )}
          </MUILink>
          <div className={clsx(classes.expandIcon, isCollapsed && classes.rotateExpandIcon)} onClick={toggleWidth}>
            <DoubleArrowIcon />
          </div>
        </div>
      </div>
    </>
  );
};
