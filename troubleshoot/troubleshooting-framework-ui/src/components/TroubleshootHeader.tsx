import { ChangeEvent, useState } from 'react';
import { useUpdateEffect } from 'react-use';
import { Box, makeStyles, MenuItem } from '@material-ui/core';
import { YBButton } from '@yugabytedb/ui-components';
import { YBSelect } from '../common/YBSelect';
import { formatData, getFilteredItems } from './TroubleshootUtils';

import { ClusterRegionSelector } from '../common/YBClusterRegionSelector';
import { ZoneNodeSelector } from '../common/YBZoneNodeSelector';
import { MetricSplitSelector } from '../common/MetricSplitSelector';
import clsx from 'clsx';
import { YBDateTimePicker } from '../common/YBDateTimePicker';
import { YBTimeFormats, formatDatetime } from '../utils/dateUtils';

const useStyles = makeStyles((theme) => ({
  refreshButton: {
    // padding: 0
  },
  selectBox: {
    minWidth: '250px'
  },
  menuItem: {
    display: 'block',
    padding: '15px 20px',
    height: '52px',
    whiteSpace: 'nowrap',
    fontSize: '14px'
  },
  overrideMuiInput: {
    '& .MuiInput-input': {
      fontWeight: 300,
      fontSize: '14px'
    },
    '& .MuiInput-root': {
      borderRadius: '8px'
    }
  },
  regularText: {
    fontWeight: 300
  }
}));

interface TroubleshootHeaderProps {
  data: any;
  selectedTab?: string;
}

export enum MetricMeasure {
  OVERALL = 'Overall',
  OUTLIER = 'Outlier',
  OUTLIER_TABLES = 'Outlier_Tables'
}

const metricSplitSelectors = [
  { value: MetricMeasure.OVERALL, label: 'Overall' },
  { value: MetricMeasure.OUTLIER, label: 'Outlier Nodes', k8label: 'Outlier Pods' },
  { value: MetricMeasure.OUTLIER_TABLES, label: 'Outlier Tables' }
] as const;

const TIME_FILTER = {
  ONE_HOUR: 'Last 1 hr',
  SIX_HOURS: 'Last 6 hrs',
  TWELVE_HOURS: 'Last 12 hrs',
  TWENTYFOUR_HOURS: 'Last 24 hrs',
  SEVEN_DAYS: 'Last 7 days',
  CUSTOM: 'Custom'
} as const;

const filterDurations = [
  { label: TIME_FILTER.ONE_HOUR, value: '1' },
  { label: TIME_FILTER.SIX_HOURS, value: '6' },
  { label: TIME_FILTER.TWELVE_HOURS, value: '12' },
  { label: TIME_FILTER.TWENTYFOUR_HOURS, value: '24' },
  { label: TIME_FILTER.SEVEN_DAYS, value: '7d' },
  { label: TIME_FILTER.CUSTOM, value: 'custom' }
] as const;

const ALL = 'All';
const ALL_REGIONS = 'All Regions and Clusters';
const ALL_ZONES = 'All Zones and Nodes';

export const TroubleshootHeader = ({ data, selectedTab }: TroubleshootHeaderProps) => {
  const {
    primaryZoneMapping,
    asyncZoneMapping,
    primaryClusterMapping,
    asyncClusterMapping
  } = formatData(data);

  const classes = useStyles();
  const [clusterRegionItem, setClusterRegionItem] = useState(ALL_REGIONS);
  const [zoneNodeItem, setZoneNodeItem] = useState(ALL_ZONES);
  const [isPrimaryCluster, setIsPrimaryCluster] = useState(true);

  const [primaryZoneToNodesMap, setPrimaryZoneToNodesMap] = useState(primaryZoneMapping);
  const [asyncZoneToNodesMap, setAsyncZoneToNodesMap] = useState(asyncZoneMapping);

  const today = new Date();
  const currentDateTime = formatDatetime(today, YBTimeFormats.YB_DATE_TIME_TIMESTAMP);

  const yesterday = new Date(today);
  yesterday.setDate(yesterday.getDate() - 1);
  const previousDateTime = formatDatetime(yesterday, YBTimeFormats.YB_DATE_TIME_TIMESTAMP);

  const [openDateTimePicker, setOpenDateTimePicker] = useState(false);
  const [datetimeError, setDateTimeError] = useState<string>('');
  const [startDateTime, setStartDateTime] = useState<Date>(yesterday);
  const [endDateTime, setEndDateTime] = useState<Date>(today);

  const [cluster, setCluster] = useState(ALL);
  const [region, setRegion] = useState(ALL);
  const [zone, setZone] = useState(ALL);
  const [node, setNode] = useState(ALL);
  const [metricMeasure, setMetricMeasure] = useState<string>(metricSplitSelectors[0].label);
  const [filterDuration, setFilterDuration] = useState<string>(filterDurations[0].label);

  useUpdateEffect(() => {
    const emptyMap = new Map();
    if (region !== ALL || cluster !== ALL) {
      const filteredZoneToNodesMap = getFilteredItems(
        isPrimaryCluster ? primaryZoneMapping : asyncZoneMapping,
        region !== ALL,
        isPrimaryCluster,
        region !== ALL ? region : cluster
      );

      if (region !== null && isPrimaryCluster) {
        setPrimaryZoneToNodesMap(filteredZoneToNodesMap);
        setAsyncZoneToNodesMap(emptyMap);
      } else if (region !== null && !isPrimaryCluster) {
        setPrimaryZoneToNodesMap(emptyMap);
        setAsyncZoneToNodesMap(filteredZoneToNodesMap);
      } else if (cluster !== null && isPrimaryCluster) {
        setPrimaryZoneToNodesMap(filteredZoneToNodesMap);
        setAsyncZoneToNodesMap(emptyMap);
      } else if (cluster !== null && !isPrimaryCluster) {
        setPrimaryZoneToNodesMap(emptyMap);
        setAsyncZoneToNodesMap(filteredZoneToNodesMap);
      }
    } else {
      setPrimaryZoneToNodesMap(primaryZoneMapping);
      setAsyncZoneToNodesMap(asyncZoneMapping);
    }
  }, [region, cluster]);

  const onSplitTypeSelected = (metricMeasure: string) => {
    setMetricMeasure(metricMeasure);
  };

  const onClusterRegionSelected = (
    isCluster: boolean,
    isRegion: boolean,
    selectedOption: string,
    isPrimaryCluster: boolean
  ) => {
    setIsPrimaryCluster(isPrimaryCluster);
    if (selectedOption === ALL_REGIONS) {
      setClusterRegionItem(ALL_REGIONS);
      setCluster(ALL);
      setRegion(ALL);
    }

    if (isCluster || isRegion) {
      setClusterRegionItem(selectedOption);

      if (isCluster) {
        setCluster(selectedOption);
        setRegion(ALL);
      }

      if (isRegion) {
        setRegion(selectedOption);
        setCluster(ALL);
      }
    }
  };

  const onZoneNodeSelected = (isZone: boolean, isNode: boolean, selectedOption: string) => {
    if (selectedOption === ALL_ZONES) {
      setZoneNodeItem(ALL_ZONES);
      setZone(ALL);
      setNode(ALL);
    }

    if (isZone || isNode) {
      setZoneNodeItem(selectedOption);
      isZone ? setZone(selectedOption) : setNode(selectedOption);
    }
  };

  const onStartDateChange = (e: ChangeEvent<HTMLInputElement>) => {
    setStartDateTime(new Date(e.target.value));
  };

  const onEndDateChange = (e: ChangeEvent<HTMLInputElement>) => {
    setEndDateTime(new Date(e.target.value));
  };

  useUpdateEffect(() => {
    if (startDateTime > endDateTime) {
      console.warn('HAMMA');
      setDateTimeError('Start Date is greater than end date');
    } else {
      setDateTimeError('');
      // Make an API call with update timestamp
    }
  }, [startDateTime, endDateTime]);

  return (
    <>
      <Box display="flex" flexDirection="column" mt={2}>
        <Box display="flex" flexDirection="row">
          <Box ml={2} mt={2.5}>
            <ClusterRegionSelector
              selectedItem={clusterRegionItem}
              onClusterRegionSelected={onClusterRegionSelected}
              primaryClusterToRegionMap={primaryClusterMapping}
              asyncClusterToRegionMap={asyncClusterMapping}
            />
          </Box>

          <Box mt={2.5}>
            <ZoneNodeSelector
              selectedItem={zoneNodeItem}
              onZoneNodeSelected={onZoneNodeSelected}
              primaryZoneToNodesMap={primaryZoneToNodesMap}
              asyncZoneToNodesMap={asyncZoneToNodesMap}
            />
          </Box>

          <Box flex={1} display="flex" flexDirection="row-reverse" alignSelf={'center'} mr={4}>
            <Box mt={2.5}>
              <YBButton
                variant="secondary"
                type="button"
                data-testid={`DiagnosticButton`}
                size="large"
                className={classes.refreshButton}
              >
                <i className="fa fa-refresh" /> Refresh
              </YBButton>
            </Box>

            <Box mr={2} mt={2.5}>
              <YBSelect
                className={clsx(classes.selectBox, classes.overrideMuiInput)}
                data-testid="time-filter-select"
                value={filterDuration}
              >
                {filterDurations.map((duration) => (
                  <MenuItem
                    key={`duration-${duration.label}`}
                    value={duration.label}
                    onClick={(e: any) => {
                      setFilterDuration(duration.label);
                      duration.label !== TIME_FILTER.CUSTOM
                        ? setOpenDateTimePicker(true)
                        : setOpenDateTimePicker(false);
                    }}
                    className={clsx(classes.menuItem, classes.regularText)}
                  >
                    {duration.label}
                  </MenuItem>
                ))}
              </YBSelect>
            </Box>
            {filterDuration === TIME_FILTER.CUSTOM && (
              <>
                <Box mr={2}>
                  <YBDateTimePicker
                    dateTimeLabel={'End Date'}
                    defaultDateTimeValue={currentDateTime}
                    onChange={onEndDateChange}
                    errorMsg={datetimeError}
                  />
                </Box>
                <Box mr={2}>
                  <YBDateTimePicker
                    dateTimeLabel={'Start Date'}
                    defaultDateTimeValue={previousDateTime}
                    onChange={onStartDateChange}
                  />
                </Box>
              </>
            )}
          </Box>
        </Box>

        <Box mt={2}>
          <MetricSplitSelector
            onSplitTypeSelected={onSplitTypeSelected}
            metricSplitSelectors={metricSplitSelectors}
          />
        </Box>
      </Box>
    </>
  );
};
