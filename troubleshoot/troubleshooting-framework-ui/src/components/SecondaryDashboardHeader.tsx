import { useEffect, useState } from 'react';
import { useUpdateEffect } from 'react-use';
import { Box, makeStyles, MenuItem } from '@material-ui/core';
import clsx from 'clsx';
import { YBButton } from '@yugabytedb/ui-components';
import { YBSelect } from '../common/YBSelect';
import { ClusterRegionSelector } from '../common/YBClusterRegionSelector';
import { OutlierSelector } from '../common/OutlierSelector';
import { ZoneNodeSelector } from '../common/YBZoneNodeSelector';
import { MetricSplitSelector } from '../common/MetricSplitSelector';
import { YBDateTimePicker } from '../common/YBDateTimePicker';
import { AppName, MetricMeasure, SplitMode, Universe } from '../helpers/dtos';
import { formatData, getFilteredItems } from '../helpers/utils';
import { YBTimeFormats, formatDatetime } from '../helpers/DateUtils';
import {
  ALL,
  metricOutlierSelectors,
  metricSplitSelectors,
  TIME_FILTER,
  filterDurations
} from '../helpers/constants';

const useStyles = makeStyles((theme) => ({
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

interface SecondaryDashboardHeaderProps {
  appName: AppName;
  // TODO: Change any to YBM/YDB response
  universeData: Universe | any;
  clusterRegionItem: string;
  zoneNodeItem: string;
  isPrimaryCluster: boolean;
  cluster: string;
  region: string;
  zone: string;
  node: string;
  metricMeasure: string;
  outlierType: string;
  filterDuration: string;
  numNodes: number;
  anomalyData: any | null;
  startDateTime: Date;
  endDateTime: Date;
  timezone?: string;
  onZoneNodeSelected: (isZone: boolean, isNode: boolean, selectedOption: string) => void;
  onClusterRegionSelected: (
    isCluster: boolean,
    isRegion: boolean,
    selectedOption: string,
    isPrimaryCluster: boolean
  ) => void;
  onOutlierTypeSelected: (selectedOption: SplitMode) => void;
  onSplitTypeSelected: (selectedOption: MetricMeasure) => void;
  onNumNodesChanged: (numNodes: number) => void;
  onSelectedFilterDuration: (selectedOption: string) => void;
  onStartDateChange: (startDate: any) => void;
  onEndDateChange: (endDate: any) => void;
}

export const SecondaryDashboardHeader = ({
  appName,
  universeData,
  clusterRegionItem,
  zoneNodeItem,
  isPrimaryCluster,
  cluster,
  region,
  zone,
  node,
  metricMeasure,
  outlierType,
  filterDuration,
  numNodes,
  anomalyData,
  startDateTime,
  endDateTime,
  timezone,
  onZoneNodeSelected,
  onClusterRegionSelected,
  onOutlierTypeSelected,
  onSplitTypeSelected,
  onNumNodesChanged,
  onSelectedFilterDuration,
  onStartDateChange,
  onEndDateChange
}: SecondaryDashboardHeaderProps) => {
  const {
    primaryZoneMapping,
    asyncZoneMapping,
    primaryClusterMapping,
    asyncClusterMapping
  } = formatData(universeData, appName);
  const classes = useStyles();

  // State variables
  const [primaryZoneToNodesMap, setPrimaryZoneToNodesMap] = useState(primaryZoneMapping);
  const [asyncZoneToNodesMap, setAsyncZoneToNodesMap] = useState(asyncZoneMapping);
  const [openDateTimePicker, setOpenDateTimePicker] = useState(false);
  const [datetimeError, setDateTimeError] = useState<string>('');

  // format date
  const currentDateTime = formatDatetime(
    endDateTime,
    YBTimeFormats.YB_DATE_TIME_TIMESTAMP,
    timezone
  );
  const previousDateTime = formatDatetime(
    startDateTime,
    YBTimeFormats.YB_DATE_TIME_TIMESTAMP,
    timezone
  );

  useEffect(() => {
    setPrimaryZoneToNodesMap(primaryZoneMapping);
  }, [primaryZoneMapping]);

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

  useUpdateEffect(() => {
    if (startDateTime > endDateTime) {
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
                data-testid={`SecondaryDashboard-DiagnosticButton`}
                size={appName == AppName.YBA ? 'large' : 'medium'}
              >
                <i className="fa fa-refresh" /> Refresh
              </YBButton>
            </Box>

            <Box mr={2} mt={2.5}>
              <YBSelect
                className={clsx(classes.selectBox, classes.overrideMuiInput)}
                data-testid={`SecondaryDashboard-TimeFilterSelect`}
                value={filterDuration}
              >
                {filterDurations.map((duration: any) => (
                  <MenuItem
                    key={`duration-${duration.label}`}
                    value={duration.label}
                    onClick={(e: any) => {
                      onSelectedFilterDuration(duration.label);
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
            anomalyData={anomalyData}
          />
        </Box>

        {metricMeasure !== MetricMeasure.OVERALL && (
          <Box mt={2}>
            <OutlierSelector
              metricOutlierSelectors={metricOutlierSelectors}
              onOutlierTypeSelected={onOutlierTypeSelected}
              onNumNodesChanged={onNumNodesChanged}
              selectedNumNodes={numNodes}
              anomalyData={anomalyData}
            />
          </Box>
        )}
      </Box>
    </>
  );
};
