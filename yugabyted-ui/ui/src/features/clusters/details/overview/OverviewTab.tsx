import React, { FC, useMemo } from 'react';
import { Box, Divider, Grid, makeStyles, MenuItem, Typography } from '@material-ui/core';
import { Paper, Button, Slide } from '@material-ui/core';
import { useTranslation, Trans } from 'react-i18next';
import { useHistory, useLocation, Link } from 'react-router-dom';
// Local imports
import { YBSelect, YBProgress} from '@app/components';
// import { YBSelect, RegionSelector } from '@app/components';
import { ClusterInfo } from './ClusterInfo';
import { ClusterInfoWidget } from './ClusterInfoWidget';
import { ClusterStatusWidget } from './ClusterStatusWidget';
import { ClusterType, countryToFlag, getRegionCode, RelativeInterval } from '@app/helpers';
import { useChartConfig } from '@app/features/clusters/details/overview/ChartConfig';
import { ChartController } from '@app/features/clusters/details/overview/ChartControler';
import { GetClusterTablesApiEnum } from '@app/api/src';
import { useGetClusterHealthCheckQuery, useGetClusterNodesQuery, useGetClusterQuery } from '@app/api/src';
import { useGetClusterTablesQuery } from '@app/api/src';
import CloseImage from '@app/assets/close.svg';
import CloseImage2 from '@app/assets/close2.svg';
import PurpleMigration from '@app/assets/purple-migration.svg';
import CloudDb from '@app/assets/clouddb.svg';
import Circle from '@app/assets/circle.svg';
import CheckedCircle from '@app/assets/circle-check-solid-green.svg';
import DatabaseMigrate from '@app/assets/database-migrate.svg';
const useStyles = makeStyles((theme) => ({
    metricsRow: {
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
    margin: theme.spacing(2, 0)
  },
  intervalPicker: {
    minWidth: 200
  },
  widgets: {
    display: 'flex',
    flexWrap: 'wrap',
    rowGap: theme.spacing(2)
  },
  dropdownTitle: {
    margin: theme.spacing(0.5, 1.5),
    fontWeight: 400,
  },
  divider: {
    margin: theme.spacing(1, 0, 1.5, 0),
  },
  stepsBox: {
    width: "fit-content",
    backgroundColor: '#f0f0f0',
    padding: theme.spacing(0.5, 1),
    fontSize: 13,
    borderRadius: theme.spacing(1.25),
    display: 'inline-block',
  },
  gradientText: {
    background: "linear-gradient(91deg, #ED35EC 3.97%, #ED35C5 33%, #5E60F0 50%)",
    backgroundClip: "text",
    WebkitBackgroundClip: "text",
    WebkitTextFillColor: "transparent",
    textDecoration: "underline",
    cursor: "pointer",
  },
  banner: {
    display: "flex",
    padding: theme.spacing(0.4, 1.5),
    width: "100%",
    marginBottom: theme.spacing(2.5),
    alignItems: "center",
    justifyContent: "space-between",
  },
  paperModal: {
    position: 'fixed',
    zIndex: 1000,
    bottom: 20,
    right: 17,
    width: 536,
    height: 390,
    backgroundColor: '#ffffff',
    borderRadius: 16,
    padding: 24,
    outline: "none"
  },
  hoverUnderline: {
    color: '#888',
    cursor: 'pointer',
    '&:hover': {
      textDecoration: 'underline',
    },
  },
  gradientTextUnderline: {
    background: "linear-gradient(91deg, #ED35EC 3.97%, #ED35C5 33%, #5E60F0 50%), " +
        "linear-gradient(91deg, #ED35EC 3.97%, #ED35C5 33%, #5E60F0 50%) " +
        "bottom 1px left 0/100% 1px no-repeat",
    backgroundClip: "text, padding-box",
    WebkitBackgroundClip: "text, padding-box",
    textFillColor: "transparent",
    WebkitTextFillColor: "transparent",
    cursor: "pointer",
    fontWeight: 600
  },
  closeBtn: {
    cursor: "pointer",
  },
  iconStyle: {
    height: 22.4,
    width: 22.4,
    margin: 0.8,
    borderRadius: '10%'
  },
  removeUnderline: {
    textDecoration: "none",
    color: "#4E5F6D",
    '&:hover': {
      textDecoration: 'underline',
    },
  }
}));

const SEARCH_PARAM_INTERVAL = 'interval';

const chartList = ['operations', 'latency'];
export const MULTI_REGION_FLAG_PATH = 'ybcloud.conf.cluster.geo_partitioning.enabled';

export const OverviewTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();
  const history = useHistory();
  const location = useLocation();
  const chartConfig = useChartConfig();
  const searchParams = new URLSearchParams(location.search);
  const translationBaseKeyGS: string = 'clusterDetail.overview.gettingStarted.getStartedSteps';
  const translationBaseKeyGS2: string = 'clusterDetail.overview.gettingStarted';
  const getStartedValue = (value: string): string => {
    return t(translationBaseKeyGS + '.' + value);
  }
  const stepsArray = [
    {
        step: 1,
        heading: getStartedValue('createCluster'),
        content: getStartedValue('createClusterDesc'),
        img: <CloudDb/>
    },
    {
        step: 2,
        heading: getStartedValue('loadCluster'),
        content: (
            <Trans
              i18nKey={translationBaseKeyGS + '.loadClusterDesc'}
              values={{
                migrationLink: getStartedValue('ybVoyager')
              }}
              components={[<Link to={`/migrations`} className={classes.removeUnderline}/>]}
            />
          ),
        img: <DatabaseMigrate/>
    }
];

  // validate interval search param to have the right value
  let interval = searchParams.get(SEARCH_PARAM_INTERVAL) as RelativeInterval;
  if (!Object.values(RelativeInterval).includes(interval)) {
    interval = RelativeInterval.LastHour;
  }
  const isUnder24Hours = (timeString: string) => {
    return Math.abs(new Date().getTime() - new Date(timeString).getTime()) < 86400000;
  };
  const changeInterval = (newInterval: string) => {
    const newLocation = { ...location };
    searchParams.set(SEARCH_PARAM_INTERVAL, newInterval);
    newLocation.search = searchParams.toString();
    history.push(newLocation); // this will trigger page re-rendering, thus no need in useState/useEffect
  };

  const { data: nodesResponse } = useGetClusterNodesQuery({});
  const regionData = useMemo(() => {
    const primarySet = new Set<string>();
    const readReplicaSet = new Set<string>();
    nodesResponse?.data.forEach(node => {
      if (!node.is_read_replica) {
        primarySet.add(node.cloud_info.region + "#" + node.cloud_info.zone)
      } else {
        readReplicaSet.add(node.cloud_info.region + "#" + node.cloud_info.zone)
      }
    });

    const getRegionItems = (regionList: string[]) =>
      regionList.map(regionZone => {
        const [region, zone] = regionZone.split('#');
        return {
          region,
          zone,
          flag: countryToFlag(getRegionCode({ region, zone })),
        }
      });

    return {
      primary: getRegionItems(Array.from(primarySet)),
      readReplica: getRegionItems(Array.from(readReplicaSet)),
    }
  }, [nodesResponse]);

  const [region, setRegion] = React.useState<string>('');
  React.useEffect(() => {
    setRegion(
      (regionData.primary.length > 0 && regionData.primary[0].region + '#'
        + regionData.primary[0].zone + '#PRIMARY') ||
      (regionData.readReplica.length > 0 && regionData.readReplica[0].region + '#' + regionData.readReplica[0].zone + '#READ_REPLICA') ||
      ''
    )
  }, [regionData]);
  const [selectedRegion, selectedZone, clusterType] = region ? region.split('#') : [undefined, undefined, undefined];

  const isMultiRegionEnabled = true;
  const [modalOpen, setModalOpen] = React.useState(true);
  const handleClose = (condition: string): void => {
    setModalOpen(false);
    condition === "temporaryClose" && sessionStorage.setItem("TEMP_CLOSE_GS_BOX", "true");
    condition === "permanentClose" && localStorage.setItem("PERMANENT_CLOSE_GS", "true");
  };
  const { data: clusterData } = useGetClusterQuery();
  const { data: tablesData } = useGetClusterTablesQuery({ api: GetClusterTablesApiEnum.Ysql });
  const [getStartedSteps ,setGetStartedSteps] = React.useState(0);
  const [_, setBannerOpen] = React.useState(true);
  const handleBannerClose = () => {
    setBannerOpen(false);
    localStorage.setItem("PERMANENT_CLOSE_BANNER", "true");
  }

  const bannerClosed = localStorage.getItem("PERMANENT_CLOSE_BANNER") === "true";

  const hasClosedPermanently: boolean = localStorage.getItem("PERMANENT_CLOSE_GS") === "true";
  const hasClosedForCurrent: boolean = sessionStorage.getItem("TEMP_CLOSE_GS_BOX") === "true";
  const isClusterRecentlyCreated =
    isUnder24Hours(String(clusterData?.data?.info.metadata.created_on));
  const hasNoTables: boolean = tablesData?.tables.length === 0;
  const showGetStarted: boolean = !hasClosedPermanently && !hasClosedForCurrent &&
    (isClusterRecentlyCreated || hasNoTables);

  const { data: healthCheckData } = useGetClusterHealthCheckQuery();
  var cluster = clusterData?.data;
  var health = healthCheckData?.data;
  //directly using useState instead of useEffect here will cause infinite rerender error,
  React.useEffect(() => {
    const stepsCompleted =
      Number(!!cluster) + Number(!hasNoTables);
    setGetStartedSteps(stepsCompleted);
  }, [clusterData, hasNoTables]);
  return (
    <>
    {!bannerClosed && hasNoTables &&
        (<Paper className={classes.banner}>
            <Box display="flex" gridGap={3} alignItems="center">
              <PurpleMigration />
              <Typography variant='body2'>
                  <Trans i18nKey={translationBaseKeyGS2 + '.tryYugabyteVoyager'}
                        values={{
                            voyagerText: getStartedValue('ybVoyager').slice(0, -2)
                        }}
                        components={[<Link to={`/migrations`}
                            className={classes.gradientTextUnderline}/>]}
                   />
              </Typography>
            </Box>
            <CloseImage onClick={handleBannerClose} className={classes.closeBtn}/>
        </Paper>)
    }
    {showGetStarted && (
          <div>
            <Slide direction="left" in={modalOpen} mountOnEnter unmountOnExit>
              <Paper elevation={10} className={classes.paperModal}>
                <Box display="flex" justifyContent="space-between"
                    alignItems="center" marginBottom={2}>
                  <Typography
                    variant="h3"
                    style={{
                      color: "#5E60F0",
                      fontWeight: 600,
                      fontSize: 18,
                    }}
                  >
                    {getStartedValue('header')}
                  </Typography>
                  <Button
                    onClick={() => handleClose("temporaryClose")}
                    style={{
                      backgroundColor: '#F0F4F7',
                      borderRadius: '50%',
                      width: 34,
                      height: 34,
                      cursor: 'pointer',
                    }}
                  >
                    <CloseImage />
                  </Button>
                </Box>

                <Divider
                  style={{
                    backgroundColor: '#D3D3D3',
                    margin: '1rem 0',
                    boxSizing: "border-box",
                    width: 'inherit',
                    marginLeft: -26,
                  }}
                />

                <Box display="flex" alignItems="center" marginBottom={2}>
                  <Typography variant="body2" style={{ marginRight: 8, height: 14 }}>
                    {getStartedSteps + " " + getStartedValue("outOfSteps")}
                  </Typography>

                  <Box width={164}>
                    <YBProgress
                      color="#13A868"
                      value={(getStartedSteps / 2) * 100}
                    />
                  </Box>
                </Box>
                {stepsArray.map((step) => (
                  <div
                    key={step.step}
                    style={{
                      display: "flex",
                      alignItems: "flex-start",
                      border: '0.063rem solid #D3D3D3',
                      borderRadius: 10,
                      padding: 16,
                      marginBottom: 16,
                    }}
                  >
                    {
                      ((step.step === 1 && !!cluster) || (step.step === 2 && !hasNoTables)) ?
                        <CheckedCircle className={classes.iconStyle} /> :
                        <Circle className={classes.iconStyle} />
                    }
                    <Box style={{ flexGrow: 1, marginLeft: 15 }}>
                      <div style={{ display: 'flex', flexDirection: 'column', color: '#4E5F6D' }}>
                        <div className={classes.stepsBox}>
                          <span style={{ fontWeight: '600' }}>Step {step.step}.</span>
                          <span>{" " + step.heading}</span>
                        </div>
                        <Typography variant="body2" style={{ marginTop: 8, width: 335 }}>
                          {step.content}
                        </Typography>
                      </div>
                    </Box>

                    <Box style={{ width: 64, height: 64 }}>
                      {step.img}
                    </Box>
                  </div>
                ))}

                <Box
                  onClick={() => handleClose("permanentClose")}
                  display="flex"
                  justifyContent="flex-end"
                  alignItems="center"
                  color="#888"
                >
                  <CloseImage2/>
                  <Typography variant="body2" className={classes.hoverUnderline}
                    style={{ marginLeft: 4}}>
                    {t('common.modalClose')}
                  </Typography>
                </Box>
              </Paper>
            </Slide>
          </div>
      )}

      <div className={classes.widgets}>
        {cluster && isMultiRegionEnabled && <ClusterInfoWidget cluster={cluster} />}
        {cluster && !isMultiRegionEnabled && <ClusterInfo cluster={cluster} />}
        {cluster && health && isMultiRegionEnabled &&
          <ClusterStatusWidget cluster={cluster} health={health} />}
      </div>
      <div className={classes.metricsRow}>
        <Box display="flex" gridGap={10} alignItems="center" width="100%">
          <Box mr={1}>
            <Typography variant="h5">{t('clusterDetail.keyMetrics')}</Typography>
          </Box>
          <YBSelect
            className={classes.intervalPicker}
            value={region}
            onChange={(event) => setRegion(event.target.value)}
          >
            {regionData.primary.length > 0 &&
              <Typography variant="body2" className={classes.dropdownTitle}>{t('clusterDetail.primaryCluster')}</Typography>
            }
            {regionData.primary.map(data => (
              <MenuItem key={data.region + '#' + data.zone + '#PRIMARY'} value={data.region + '#' + data.zone + '#PRIMARY'}>
                {data.flag && <Box mr={1}>{data.flag}</Box>} {data.region} ({data.zone})
              </MenuItem>
            ))}
            {regionData.primary.length > 0 && regionData.readReplica.length > 0 &&
              <Divider className={classes.divider} />
            }
            {regionData.readReplica.length > 0 &&
                <Typography variant="body2" className={classes.dropdownTitle}>{t('clusterDetail.readReplicas')}</Typography>
            }
            {regionData.readReplica.map(data => (
              <MenuItem key={data.region + '#' + data.zone + '#READ_REPLICA'} value={data.region + '#' + data.zone + '#READ_REPLICA'}>
                {data.flag && <Box mr={1}>{data.flag}</Box>} {data.region} ({data.zone})
              </MenuItem>
            ))}
          </YBSelect>
          <YBSelect
            className={classes.intervalPicker}
            value={interval}
            onChange={(event) => changeInterval(event.target.value)}
          >
            <MenuItem value={RelativeInterval.LastHour}>
                {t('clusterDetail.lastHour')}
            </MenuItem>
            <MenuItem value={RelativeInterval.Last6Hours}>
                {t('clusterDetail.last6hours')}
            </MenuItem>
            <MenuItem value={RelativeInterval.Last12hours}>
                {t('clusterDetail.last12hours')}
            </MenuItem>
            <MenuItem value={RelativeInterval.Last24hours}>
                {t('clusterDetail.last24hours')}
            </MenuItem>
            {/*<MenuItem value={RelativeInterval.Last7days}>
                {t('clusterDetail.last7days')}
            </MenuItem>*/}
          </YBSelect>
        </Box>
      </div>
      <Grid container spacing={2}>
        {chartList.map((chart: string) => {
          const config = chartConfig[chart];
          return (
            config && (
              <Grid key={chart} item xs={6}>
                <ChartController
                  title={config.title}
                  metric={config.metric}
                  unitKey={config.unitKey}
                  metricChartLabels={config.chartLabels}
                  strokes={config.strokes}
                  chartDrawingType={config.chartDrawingType}
                  relativeInterval={interval}
                  regionName={selectedRegion}
                  zone={selectedZone}
                  clusterType={clusterType as (ClusterType | undefined)}
                />
              </Grid>
            )
          );
        })}
      </Grid>
    </>
  );
};
