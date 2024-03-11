import { ChangeEvent, useState, FC } from 'react';
import { Box } from '@material-ui/core';
import clsx from 'clsx';
import { YBCheckbox } from '@yugabytedb/ui-components';
import { IssueMetadata } from './IssueMetadata';
import { AnomalyCategory, Anomaly, AppName } from '../helpers/dtos';
import { assertUnreachableCase } from '../helpers/ErrorHandlingUtils';
import { useStyles } from './styles';

import TraingleDownIcon from '../assets/traingle-down.svg';
import TraingleUpIcon from '../assets/traingle-up.svg';

interface PrimaryDashboardProps {
  category: AnomalyCategory;
  data: Anomaly;
  baseUrl?: string;
  key: string;
  uuid: string;
  idKey: string;
  resolved: boolean;
  universeUuid: string;
  appName: AppName;
  timezone?: string;
  onResolve: (key: string, value: boolean) => void;
}

export const PrimaryDashboard: FC<PrimaryDashboardProps> = ({
  idKey,
  category,
  uuid,
  data,
  baseUrl,
  resolved,
  universeUuid,
  appName,
  timezone,
  onResolve
}) => {
  const classes = useStyles();

  // State variables
  const [open, setOpen] = useState(false);

  const handleResolveAnomaly = (event: ChangeEvent<HTMLInputElement>) => {
    const isChecked = event.target.checked;
    onResolve(idKey, isChecked);
    if (isChecked) {
      setOpen(false);
    }
    event.stopPropagation();
  };

  const handleOpenBox = () => {
    if (!resolved) {
      setOpen((val) => !val);
    }
  };

  const getAnomalyMetadata = (title: string, data: Anomaly, uuid: string) => {
    return (
      <IssueMetadata
        data={data}
        title={title}
        uuid={uuid}
        universeUuid={universeUuid}
        appName={appName}
        baseUrl={baseUrl}
        timezone={timezone}
      />
    );
  };

  const getAnomalyCategoryColor = (anomalyCategory: AnomalyCategory) => {
    switch (anomalyCategory) {
      case AnomalyCategory.APP:
        return (
          <span className={clsx(classes.troubleshootTitle, classes.tagGreen)}>{'APP ISSUE'}</span>
        );
      case AnomalyCategory.INFRA:
        return (
          <span className={clsx(classes.troubleshootTitle, classes.tagGreen)}>{'INFRA ISSUE'}</span>
        );
      case AnomalyCategory.NODE:
        return (
          <span className={clsx(classes.troubleshootTitle, classes.tagGreen)}>{'NODE ISSUE'}</span>
        );
      case AnomalyCategory.SQL:
        return (
          <span className={clsx(classes.troubleshootTitle, classes.tagBlue)}>{'SQL ISSUE'}</span>
        );
      case AnomalyCategory.DB:
        return (
          <span className={clsx(classes.troubleshootTitle, classes.tagBlue)}>{'DB ISSUE'}</span>
        );
      default:
        return assertUnreachableCase(anomalyCategory);
    }
  };

  const getAnomalyTitle = (anomalyData: Anomaly) => {
    return anomalyData.title;
  };

  const getAnomalySummary = (anomalyData: Anomaly) => {
    return anomalyData.summary;
  };

  return (
    <Box className={classes.recommendation}>
      <Box
        onClick={handleOpenBox}
        display="flex"
        alignItems="center"
        className={resolved ? classes.strikeThroughText : classes.itemHeader}
      >
        {getAnomalyCategoryColor(category)}
        <span>{!open && getAnomalySummary(data)}</span>
        <Box ml="auto">
          <YBCheckbox label={'Resolved'} onChange={handleResolveAnomaly} checked={resolved} />
        </Box>
        {open ? (
          <img src={TraingleDownIcon} alt="expand" />
        ) : (
          <img
            src={TraingleUpIcon}
            alt="shrink"
            className={clsx(resolved && classes.inactiveIssue)}
          />
        )}
      </Box>
      {open && getAnomalyMetadata(getAnomalyTitle(data), data, uuid)}
    </Box>
  );
};
