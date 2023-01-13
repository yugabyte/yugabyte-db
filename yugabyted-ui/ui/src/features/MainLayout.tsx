import React, { FC } from 'react';
import { Switch, Route, Redirect } from 'react-router-dom';
import { Container, makeStyles } from '@material-ui/core';
import { Header } from '@app/features/Header';
import { Footer } from '@app/features/Footer';
import { Sidebar } from '@app/features/Sidebar';
import { ClustersRouting } from '@app/features/clusters/ClustersRouting';
import { themeVariables } from '@app/theme/variables';
import { PerformanceRouting } from './clusters/PerformanceRouting';
import { GettingStarted } from './welcome/GettingStarted';
import { OverviewRouting } from '@app/features/clusters/OverviewRouting';
import { DatabasesRouting } from '@app/features/clusters/DatabasesRouting';

const useStyles = makeStyles((theme) => ({
  root: {
    display: 'flex',
    position: 'relative',
    margin: 0,
    padding: 0,
    overflow: 'auto',
    height: '100%',
    minHeight: '100vh'
  },
  main: {
    position: 'relative',
    width: '100%',
    minWidth: themeVariables.screenMinWidth,
    minHeight: themeVariables.screenMinHeight // For short pages with sparse content
  },
  mainContainer: {
    paddingBottom: themeVariables.footerHeight + theme.spacing(3) // space for the absolute positioned footer
  }
}));

// const verifyPathParams = (
//   routeParam: Record<string, string>,
//   linkedAccounts: undefined,
//   projectData:  undefined
// ) => {
//   return (Component: FC<RouteComponentProps>) => {
//     // eslint-disable-next-line react/display-name
//     return (props: RouteComponentProps): ReactNode => {
//       const { accountId, projectId } = routeParam;
//       if ((props.match.params as RouteParams)?.accountId !== undefined) {
//         // if (
//         //   linkedAccounts?.find((account) => account?.info?.id === (props.match.params as RouteParams)?.accountId) ===
//         //   undefined
//         // ) {
//           return <Redirect to={generatePath(props.match.path, { accountId, projectId })} />;
//         // }
//       }

//       if ((props.match.params as RouteParams)?.projectId !== undefined) {
//         // if (
//         //   projectData?.find((project) => project?.id === (props.match.params as RouteParams)?.projectId) === undefined
//         // ) {
//           return <Redirect to={generatePath(props.match.path, { accountId, projectId })} />;
//         // }
//       }
//       return <Component {...props} />;
//     };
//   };
// };

export const MainLayout: FC = () => {
  const classes = useStyles();

  let projectId: string | undefined;
  // if (projectData?.data) {
  //   const [project] = projectData?.data || [];
  //   projectId = project?.id;
  // }

  // render all protected content when auth token successfully validated by account/projects queries
  return (
    <div className={classes.root}>
      {/* <Route path="/a/:accountId"> */}
      <Route>
        <Sidebar projectId={projectId ?? ''} />
      </Route>
      <main className={classes.main}>
        {/* <Route path="/a/:accountId"> */}
        <Route>
          <Header />
        </Route>
        <Container maxWidth={false} className={classes.mainContainer}>
          {/* TODO: uncomment when all announced feature will be available without feature flags, including VPC
          <Announcement />
          */}
          <Switch>
              <Route path={'/overview'} component={OverviewRouting} />
              <Route path={'/databases'} component={DatabasesRouting} />
              <Route path={'/cluster'} component={ClustersRouting} />
              <Route path={'/performance'} component={PerformanceRouting} />
              <Route path={'/alerts'} component={GettingStarted} />
              <Route path={'/debug'} component={GettingStarted} />
              <Route exact path={'/'} render={() => {
                  return (
                    <Redirect to="/overview" />
                  )
              }

              } />
            {/* <Route path={'/a/:accountId/p/:projectId/network/:tab?'} render={verifyPathParamsMemo(Network)} />
            <Route path={'/a/:accountId/profile'} render={verifyPathParamsMemo(Profile)} />
            <Route path={'/a/:accountId/p/:projectId/analytics'} render={verifyPathParamsMemo(AnalyticsRouting)} />
            <Route path={'/a/:accountId/p/:projectId/clusters'} render={verifyPathParamsMemo(ClustersRouting)} />            */}

          </Switch>
        </Container>
        <Footer />
      </main>
    </div>
  );
};
