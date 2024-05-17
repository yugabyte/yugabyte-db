import { useState, useEffect, ChangeEvent } from 'react';
import { useUpdateEffect } from 'react-use';
import { Box, MenuItem, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import {
  YBSelect,
  YBDateTimePicker,
  YBTimeFormats,
  formatDatetime,
  getDiffHours
} from '@yugabytedb/ui-components';
import { PrimaryDashboard } from './PrimaryDashboard';
import { Anomaly, AnomalyCategory, AppName } from '../helpers/dtos';
import { TIME_FILTER, anomalyFilterDurations } from '../helpers/constants';

interface PrimaryDashboardDataProps {
  anomalyData: Anomaly[] | null;
  appName: AppName;
  timezone?: string;
  universeUuid: string;
  onSelectedIssue?: (troubleshootUuid: string) => void;
  onFilterByDate: (startDate: any, endDate: any) => void;
}

const useStyles = makeStyles((theme) => ({
  troubleshootAdvisorBox: {
    marginRight: theme.spacing(1)
  },
  selectBox: {
    minWidth: '180px',
    maxWidth: '220px'
  },
  flexRow: {
    display: 'flex',
    flexDirection: 'row'
  },
  dateBox: {
    alignItems: 'flex-start'
  },
  contentBox: {
    justifyContent: 'space-between'
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
      borderRadius: '8px 8px 8px 8px'
    },
    '& .MuiMenu-list': {
      maxHeight: '400px'
    }
  },
  regularText: {
    fontWeight: 300
  },
  datePicker: {
    marginRight: theme.spacing(2),
    marginTop: '-17px'
  }
}));

const ANAMOLY_CATEGORY_LIST = [
  AnomalyCategory.SQL,
  AnomalyCategory.NODE,
  AnomalyCategory.DB,
  AnomalyCategory.APP,
  AnomalyCategory.INFRA
];

export const PrimaryDashboardData = ({
  anomalyData,
  appName,
  universeUuid,
  timezone,
  onSelectedIssue,
  onFilterByDate
}: PrimaryDashboardDataProps) => {
  const classes = useStyles();
  const today = new Date();
  const yesterday = new Date(today);
  yesterday.setDate(yesterday.getDate() - 14);

  const [filteredAnomalyList, setFilteredAnomalyList] = useState<Anomaly[]>([]);
  const [anomalyCategoryOptions, setAnomalyCategoryOptions] = useState<AnomalyCategory[]>();
  const [filterDuration, setFilterDuration] = useState<string>(anomalyFilterDurations[0].value);
  const [datetimeError, setDateTimeError] = useState<string>('');
  const [startDateTime, setStartDateTime] = useState<Date>(yesterday);
  const [endDateTime, setEndDateTime] = useState<Date>(today);

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
    setFilteredAnomalyList(anomalyData!);
    setAnomalyCategoryOptions(anomalyData?.map((anomaly: Anomaly) => anomaly.category));
  }, [anomalyData]);

  useUpdateEffect(() => {
    if (startDateTime > endDateTime) {
      setDateTimeError('Start Date is greater than end date');
    } else if (getDiffHours(startDateTime, endDateTime) < 24) {
      setDateTimeError('Time difference should be minimum of 24 hours');
    } else {
      // Make an API call with update timestamp
      setDateTimeError('');
      // Investigate if we can just do toISOString();
      const formattedStartDate = startDateTime?.toISOString();
      const formattedEndDate = endDateTime?.toISOString();
      onFilterByDate(formattedStartDate, formattedEndDate);
    }
  }, [startDateTime, endDateTime]);

  useUpdateEffect(() => {
    // When the default window is 14 days
    if (filterDuration === anomalyFilterDurations[0].value) {
      const formattedStartDate = yesterday?.toISOString();
      const formattedEndDate = today?.toISOString();
      onFilterByDate(formattedStartDate, formattedEndDate);
    }
  }, [filterDuration]);

  const handleResolve = (id: string, isResolved: boolean) => {
    const anomalyListCopy = JSON.parse(JSON.stringify(anomalyData));
    const selectedAnomaly = anomalyListCopy?.find((anomaly: Anomaly) => anomaly.uuid === id);
    if (selectedAnomaly) {
      selectedAnomaly.isResolved = isResolved;
    }
    setFilteredAnomalyList(anomalyListCopy);
  };

  const onSelectCategories = (category: AnomalyCategory | string) => {
    const anomalyListCopy = JSON.parse(JSON.stringify(anomalyData));
    const filteredAnomalies =
      category !== 'ALL'
        ? anomalyListCopy?.filter((anomaly: Anomaly) => anomaly.category === category)
        : anomalyData;
    setFilteredAnomalyList(filteredAnomalies);
  };

  const onSelectedFilterDuration = (filterDuration: string) => {
    setFilterDuration(filterDuration);
  };

  const onStartDateChange = (e: ChangeEvent<HTMLInputElement>) => {
    setStartDateTime(new Date(e.target.value));
  };

  const onEndDateChange = (e: ChangeEvent<HTMLInputElement>) => {
    setEndDateTime(new Date(e.target.value));
  };

  return (
    <Box>
      <Box className={clsx(classes.flexRow, classes.contentBox)}>
        <Box>
          <YBSelect
            className={clsx(classes.selectBox, classes.overrideMuiInput)}
            // inputProps={{ IconComponent: () => null }}
          >
            <MenuItem
              className={classes.menuItem}
              onClick={() => {
                onSelectCategories('ALL');
              }}
            >
              {'ALL'}
            </MenuItem>
            {ANAMOLY_CATEGORY_LIST?.map((category: AnomalyCategory, idx: number) => {
              return (
                <MenuItem
                  className={classes.menuItem}
                  key={idx}
                  disabled={!anomalyCategoryOptions?.includes(category)}
                  value={category}
                  onClick={() => {
                    onSelectCategories(category);
                  }}
                >
                  {`${category} ISSUE`}
                </MenuItem>
              );
            })}
          </YBSelect>
        </Box>
        <Box className={classes.flexRow}>
          <YBSelect
            className={clsx(classes.selectBox, classes.overrideMuiInput)}
            data-testid={`PrimaryDashboard-TimeFilterSelect`}
            value={filterDuration}
          >
            {anomalyFilterDurations.map((duration: any) => (
              <MenuItem
                key={`duration-${duration.label}`}
                value={duration.value}
                onClick={(e: any) => {
                  onSelectedFilterDuration(duration.value);
                }}
                className={clsx(classes.menuItem, classes.regularText)}
              >
                {duration.label}
              </MenuItem>
            ))}
          </YBSelect>
          {filterDuration === TIME_FILTER.SMALL_CUSTOM && (
            <>
              <Box className={classes.datePicker}>
                <YBDateTimePicker
                  dateTimeLabel={'Start Date'}
                  defaultDateTimeValue={previousDateTime}
                  onChange={onStartDateChange}
                />
              </Box>
              <Box className={classes.datePicker}>
                <YBDateTimePicker
                  dateTimeLabel={'End Date'}
                  defaultDateTimeValue={currentDateTime}
                  onChange={onEndDateChange}
                  errorMsg={datetimeError}
                />
              </Box>
            </>
          )}
        </Box>
      </Box>

      <Box className={classes.troubleshootAdvisorBox} mt={3}>
        {filteredAnomalyList?.map((rec: any) => (
          <>
            <PrimaryDashboard
              key={rec.uuid}
              idKey={rec.uuid}
              uuid={rec.uuid}
              category={rec.category}
              data={rec}
              resolved={!!rec.isResolved}
              onResolve={handleResolve}
              universeUuid={universeUuid}
              appName={appName}
              timezone={timezone}
              onSelectedIssue={onSelectedIssue}
            />
          </>
        ))}
      </Box>
    </Box>
  );
};
