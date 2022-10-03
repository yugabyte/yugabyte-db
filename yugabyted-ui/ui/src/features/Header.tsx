import React, { FC } from 'react';
import {
  AppBar,
  Box,
  makeStyles,
  MenuItem,
  Toolbar,
  Divider,
  Typography,
  IconButton,
  Link as MUILink
} from '@material-ui/core';
import {  Route, Switch, useRouteMatch } from 'react-router-dom';
import { useTranslation } from 'react-i18next';

import { YBDropdown } from '@app/components';
import { ClusterHeader } from '@app/features/clusters/ClusterHeader';
import HelpIcon from '@app/assets/help.svg';
import FileIcon from '@app/assets/file.svg';
import SlackIcon from '@app/assets/slack.svg';

const useStyles = makeStyles((theme) => ({
  divider: {
    width: `calc(100% - ${theme.spacing(4)}px)`
  },
  toRight: {
    marginLeft: 'auto',
    display: 'flex',
    alignItems: 'center'
  },
  sendFeedback: {
    color: theme.palette.grey[600],
    display: 'flex',
    paddingRight: theme.spacing(1.5)
  },
  menuIcon: {
    marginRight: theme.spacing(1),
    color: theme.palette.grey[600]
  },
  userNameItem: {
    height: 'auto',
    '&:hover,&:focus': {
      backgroundColor: 'unset',
      cursor: 'default'
    }
  }
}));

const LINK_DOCUMENTATION = 'https://docs.yugabyte.com/preview/explore/';
export const LINK_SUPPORT = 'https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431';
const LINK_SLACK = 'https://yugabyte-db.slack.com/';

export const Header: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();
  const { path } = useRouteMatch<App.RouteParams>();
  // const history = useHistory();
  // const queryClient = useQueryClient();

  return (
    <AppBar position="static" color="transparent">
      <Toolbar>
        <Switch>
          <Route path={'/a/add/account'}>
            <Typography variant="h4" color="inherit">
              {t('common.addAccount')}
            </Typography>
          </Route>
          <Route path={`/admin`}>
            <Typography variant="h4" color="inherit">
              {t('common.admin')}
            </Typography>
          </Route>
          <Route path={`${path}/p/:projectId/analytics`}>
            {/* <ProjectPicker /> */}
            <Typography variant="h4" color="inherit">
              {t('common.analytics')}
            </Typography>
          </Route>
          <Route path={`/cluster`}>
            {/* <ProjectPicker /> */}
            <ClusterHeader />
          </Route>
          <Route path={`/performance`}>
            <Typography variant="h4" color="inherit">
              {t('clusterDetail.tabPerformance')}
            </Typography>
          </Route>
          <Route path={`${path}/dbsecurity`}>
            <Typography variant="h4" color="inherit">
              {t('common.dbSecurity')}
            </Typography>
          </Route>
          <Route path={`${path}/p/:projectId/network`}>
            <Typography variant="h4" color="inherit">
              {t('common.networkAccess')}
            </Typography>
          </Route>
          <Route path={`${path}/profile`}>
            <Typography variant="h4" color="inherit">
              {t('common.userProfile')}
            </Typography>
          </Route>
          <Route path={`${path}/p/:projectId/welcome`}>
            <Typography variant="h4" color="inherit">
              {t('welcome.gettingStarted')}
            </Typography>
          </Route>
          <Route path={`/alerts`}>
            <Typography variant="h4" color="inherit">
              {t('common.alerts')}
            </Typography>
          </Route>
        </Switch>
        <div className={classes.toRight}>
          <Box display="flex">
            <MUILink className={classes.sendFeedback} href={LINK_SLACK} target="_blank">
              <SlackIcon  className={classes.menuIcon} />
              <Typography variant="body2">{t('common.joinSlack')}</Typography>
            </MUILink>
          </Box>
          <YBDropdown
            origin={
              <IconButton>
                <HelpIcon />
              </IconButton>
            }
          >
            <MenuItem component="a" href={LINK_DOCUMENTATION} target="_blank">
              <FileIcon className={classes.menuIcon} />
              <Typography variant="body2">{t('common.documentation')}</Typography>
            </MenuItem>
            {/* <MenuItem component="a" href={LINK_SLACK} target="_blank">
              <Box width={24} mr={1} display="flex" justifyContent="center">
                <SlackIcon />
              </Box>
              <Typography variant="body2">{t('common.ybSlack')}</Typography>
            </MenuItem> */}
          </YBDropdown>
        </div>
        <Divider orientation="horizontal" variant="middle" absolute className={classes.divider} />
      </Toolbar>
    </AppBar>
  );
};
