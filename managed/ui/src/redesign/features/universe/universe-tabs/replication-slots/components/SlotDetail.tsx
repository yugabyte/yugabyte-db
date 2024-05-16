import { FC, useState } from 'react';
import _ from 'lodash';
import { Link } from 'react-router';
import { RouteComponentProps } from 'react-router-dom';
import moment, { DurationInputArg1 } from 'moment';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { Box, Typography, Grid } from '@material-ui/core';
import { YBWidget } from '../../../../../../components/panels';
import { YBLoading } from '../../../../../../components/common/indicators';
import { MetricsPanelOverview } from '../../../../../../components/metrics';
import { YBButtonLink } from '../../../../../../components/common/forms/fields';
import { CustomDatePicker } from '../../../../../../components/metrics/CustomDatePicker/CustomDatePicker';
import { QUERY_KEY, api } from '../../../../../utils/api';
import {
  fetchCDCAllMetrics,
  CDC_METRIC_ARRAY,
  CDC_METRICS_COLOR,
  TIME_RANGE_FILTER_TYPES,
  INTERVAL_TYPES,
  CDC_LAG_KEY,
  CDC_EXPIRY_TIME_KEY
} from '../utils/helper';
import { formatDuration } from '../../../../../../utils/Formatters';
import { isNonEmptyObject } from '../../../../../../utils/ObjectUtils';
import { ReplicationSlotResponse, SlotState } from '../utils/types';
import { METRIC_COLORS } from '../../../../../../components/metrics/MetricsConfig';
import { DurationUnit } from '../../../../../../components/xcluster/disasterRecovery/constants';
import { replicationSlotStyles } from '../utils/ReplicationSlotStyles';

interface SlotDetailProps {
  uuid: string;
  streamID: string;
}

export const SlotDetail: FC<RouteComponentProps<{}, SlotDetailProps>> = ({ location, params }) => {
  const { uuid, streamID } = params;
  const classes = replicationSlotStyles();
  const customer = useSelector((state: any) => state.customer);
  const [refreshInterval, setRefreshInterval] = useState<any>(INTERVAL_TYPES[0].value);
  const [filterLabel, setFilterLabel] = useState<any>(TIME_RANGE_FILTER_TYPES[0].label);
  const [startMoment, setStartMoment] = useState<any>(moment().subtract('1', 'hour'));
  const [endMoment, setEndMoment] = useState<any>(moment());
  const { t } = useTranslation();
  //fetch Universe details to get nodePrefix details
  const { data: universeData, isLoading: isUniverseDataLoading } = useQuery([uuid], () =>
    api.fetchUniverse(uuid)
  );

  //fetch ReplicationSlot Data to paint replication data details
  const { data: replicationSlotData, isLoading: isReplicationSlotLoading } = useQuery<
    ReplicationSlotResponse
  >([QUERY_KEY.getReplicationSlots, uuid], () => api.getReplicationSlots(uuid));

  //fetch Metrics data for each streamID
  const { data: metricsData, isLoading: isMetricsLoading, refetch, isFetching } = useQuery(
    [streamID],
    () => {
      const startFilter = TIME_RANGE_FILTER_TYPES.find((f) => f.label === filterLabel);
      const graphFilter = {
        streamId: streamID,
        nodePrefix: universeData?.universeDetails.nodePrefix,
        start:
          filterLabel === 'Custom'
            ? moment(startMoment).utc().format('X')
            : moment()
                .utc()
                .subtract(
                  startFilter?.value as DurationInputArg1,
                  startFilter?.type as DurationUnit
                )
                .format('X'),
        end:
          filterLabel === 'Custom'
            ? moment(endMoment).utc().format('X')
            : moment().utc().format('X')
      };
      return fetchCDCAllMetrics(graphFilter);
    },
    {
      enabled: !!universeData?.universeDetails.nodePrefix,
      refetchInterval: refreshInterval === 'Off' ? false : refreshInterval
    }
  );

  //refetch if filter types get changes
  useUpdateEffect(() => {
    if (filterLabel != 'Custom') refetch();
  }, [filterLabel]);

  //Custom Filter methods
  const handleStartDateChange = (dateStr: any) => setStartMoment(dateStr);
  const handleEndDateChange = (dateStr: any) => setEndMoment(dateStr);
  const applyCustomFilter = () => refetch();

  const refreshIntervalLabel = () => {
    const label = INTERVAL_TYPES.find((interval) => interval.value === refreshInterval)
      ?.selectedLabel;
    return label;
  };

  const intervalMenuItems = INTERVAL_TYPES.map((interval, idx) => {
    const key = 'graph-interval-' + idx;
    return (
      <MenuItem
        onSelect={() => setRefreshInterval(interval.value)}
        key={key}
        eventKey={idx}
        active={interval.value === refreshInterval}
      >
        {interval.label}
      </MenuItem>
    );
  });

  const filterMenuItems = TIME_RANGE_FILTER_TYPES.map((filter, idx) => {
    const key = 'graph-filter-' + idx;
    if (filter.type === 'divider') {
      return <MenuItem divider key={key} />;
    }
    return (
      <MenuItem
        onSelect={() => setFilterLabel(filter.label)}
        data-filter-type={filter.type}
        key={key}
        eventKey={idx}
        active={filter.label === filterLabel}
      >
        {filter.label}
      </MenuItem>
    );
  });

  if (isUniverseDataLoading || isReplicationSlotLoading || isMetricsLoading) return <YBLoading />;
  const slotDetail = replicationSlotData?.replicationSlots?.find((r) => r?.streamID === streamID);

  const slotStatus = slotDetail?.state ? (
    <Typography
      className={
        [SlotState.INITIATED, SlotState.ACTIVE].includes(slotDetail?.state)
          ? classes.successStatus
          : classes.errorStatus
      }
    >
      {_.capitalize(slotDetail?.state)}
    </Typography>
  ) : null;

  const renderBreadCrumbs = () => {
    return (
      <Box className={classes.slotDetailHeader}>
        <Typography className={classes.slotUniverseTitle}>
          {universeData?.universeDetails?.clusters[0]?.userIntent?.universeName}
        </Typography>
        <Typography className={classes.slotReplicationTitle}>
          <i className="fa fa-chevron-right"></i>
        </Typography>
        <Link to={`/universes/${uuid}/replication-slots`} className={classes.slotReplicationTitle}>
          {t('cdc.replicationSlot')}
        </Link>
        <Typography className={classes.slotReplicationTitle}>
          <i className="fa fa-chevron-right"></i>
        </Typography>
        <Typography className={classes.slotReplicationTitle}>{slotDetail?.slotName}</Typography>{' '}
        &nbsp;&nbsp;
        {slotStatus}
      </Box>
    );
  };

  const renderSlotDetails = () => {
    return (
      <Box className={classes.slotMainDetails}>
        <Box display={'flex'} flexDirection={'row'}>
          <Typography variant="body1">{t('cdc.name')} :</Typography> &nbsp;
          <Typography variant="body2">{slotDetail?.slotName}</Typography>
        </Box>
        <Box display={'flex'} flexDirection={'row'}>
          <Typography variant="body1">{t('cdc.database')} :</Typography> &nbsp;
          <Typography variant="body2">{slotDetail?.databaseName}</Typography>
        </Box>
      </Box>
    );
  };

  const renderTimeWidget = (milliseconds: number, title: string) => {
    return (
      <Box display="flex" flexDirection={'column'} alignItems="center">
        <Box display="flex" flexDirection={'row'}>
          <Typography className={classes.metricHiglightNumber}>
            {isFetching ? '--' : formatDuration(milliseconds)}
          </Typography>
        </Box>
        <Box mt={1} mb={1} className={classes.metricDivider}></Box>
        <Typography className={classes.metricHighlightsTitle}>{title}</Typography>
      </Box>
    );
  };

  const renderHighlights = () => {
    const currentLag = Number(_.last(metricsData?.[CDC_LAG_KEY]?.data[0]?.y));
    const expiryTime = Number(_.last(metricsData?.[CDC_EXPIRY_TIME_KEY]?.data[0]?.y)) * 60 * 1000;
    return (
      <>
        {renderTimeWidget(currentLag, t('cdc.currentLag'))}
        {renderTimeWidget(expiryTime, t('cdc.timeToExpiry'))}
        <Box display="flex" flexDirection={'column'} alignItems="center">
          <Box display="flex" flexDirection={'row'}>
            {slotStatus}
          </Box>
          <Box mt={1} mb={1} className={classes.metricDivider}></Box>
          <Typography className={classes.metricHighlightsTitle}>
            {t('cdc.replicationStatus')}
          </Typography>
        </Box>
      </>
    );
  };

  const renderMetrics = () => {
    return (
      <Box display={'flex'} width="100%">
        <Grid container spacing={2}>
          {CDC_METRIC_ARRAY.map((metricKey) => {
            if (isNonEmptyObject(metricsData?.[metricKey]) && !metricsData?.[metricKey]?.error) {
              let metrics = metricsData?.[metricKey];
              for (let idx = 0; idx < metrics.data.length; idx++) {
                metrics.data[idx].line = {
                  color: idx === 0 ? CDC_METRICS_COLOR[metricKey] : METRIC_COLORS[idx],
                  width: 1.5
                };
              }
              const metricTickSuffix = _.get(metrics, 'layout.yaxis.ticksuffix');
              const measureUnit = metricTickSuffix
                ? ` (${metricTickSuffix.replace('&nbsp;', '')})`
                : '';

              return (
                <Grid item key={metricKey} sm={6} md={6} lg={4}>
                  <YBWidget
                    className={classes.metricWidget}
                    noMargin
                    headerLeft={
                      <div className="metric-title">
                        <span>{metrics?.layout?.title + measureUnit}</span>
                      </div>
                    }
                    body={
                      isFetching ? (
                        <Box
                          width="100%"
                          height="100%"
                          display={'flex'}
                          alignItems={'center'}
                          justifyContent={'center'}
                        >
                          <YBLoading />
                        </Box>
                      ) : (
                        <MetricsPanelOverview
                          key={metricKey}
                          customer={customer}
                          metricKey={metricKey}
                          metric={metrics}
                          className={'metrics-panel-container'}
                          height={300}
                        />
                      )
                    }
                  />
                </Grid>
              );
            }
            return null;
          })}
        </Grid>
      </Box>
    );
  };
  return (
    <Box className={classes.slotDetailContainer}>
      {renderBreadCrumbs()}
      <Box p={4} width="100%" display={'flex'} flexDirection={'column'}>
        {renderSlotDetails()}
        <Box mt={6} className={classes.slotMetricContainer}>
          <Box display={'flex'} justifyContent={'flex-end'}>
            <div className="graph-interval-container">
              <Dropdown id="graph-interval-dropdown" pullRight>
                <Dropdown.Toggle className="dropdown-toggle-button">
                  {t('cdc.autoRefresh')}:&nbsp;
                  <span className="chip" key={`interval-token`}>
                    <span className="value">{refreshIntervalLabel()}</span>
                  </span>
                </Dropdown.Toggle>
                <Dropdown.Menu>{intervalMenuItems}</Dropdown.Menu>
              </Dropdown>
              <YBButtonLink
                btnIcon={'fa fa-refresh'}
                btnClass="btn btn-default refresh-btn"
                onClick={() => refetch()}
              />
            </div>
          </Box>
          <Box mt={3} className={classes.metricHiglights}>
            {renderHighlights()}
          </Box>
          <Box mt={3} mb={3} display={'flex'} justifyContent={'flex-end'}>
            <div className=" date-picker pull-right">
              {filterLabel === 'Custom' && (
                <CustomDatePicker
                  startMoment={startMoment}
                  endMoment={endMoment}
                  handleTimeframeChange={applyCustomFilter}
                  setStartMoment={handleStartDateChange}
                  setEndMoment={handleEndDateChange}
                />
              )}
              <Dropdown id="graphFilterDropdown" pullRight>
                <Dropdown.Toggle>
                  <i className="fa fa-clock-o"></i>&nbsp; {filterLabel}
                </Dropdown.Toggle>
                <Dropdown.Menu>{filterMenuItems}</Dropdown.Menu>
              </Dropdown>
            </div>
          </Box>
          {renderMetrics()}
        </Box>
      </Box>
    </Box>
  );
};
