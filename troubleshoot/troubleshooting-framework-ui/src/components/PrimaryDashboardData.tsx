import { useState, useEffect } from 'react';
import { Box, MenuItem, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { PrimaryDashboard } from './PrimaryDashboard';
import { ANOMALY_DATA_LIST } from './MockAnomalyData';
import { Anomaly, AnomalyCategory, AppName } from '../helpers/dtos';
import { YBSelect } from '../common/YBSelect';

interface PrimaryDashboardDataProps {
  anomalyData: Anomaly[] | null;
  appName: AppName;
  baseUrl?: string;
  timezone?: string;
}

const useStyles = makeStyles((theme) => ({
  troubleshootAdvisorBox: {
    marginRight: theme.spacing(1)
  },
  selectBox: {
    minWidth: '180px',
    maxWidth: '220px'
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
  baseUrl,
  timezone
}: PrimaryDashboardDataProps) => {
  const classes = useStyles();
  const [anomalyList, setAnomalyList] = useState<Anomaly[]>(ANOMALY_DATA_LIST);
  const [anomalyCategoryOptions, setAnomalyCategoryOptions] = useState<AnomalyCategory[]>();

  useEffect(() => {
    setAnomalyCategoryOptions(anomalyList.map((anomaly: Anomaly) => anomaly.category));
  }, []);

  const handleResolve = (id: string, isResolved: boolean) => {
    const anomalies: any = [...ANOMALY_DATA_LIST];
    const selectedAnomaly = anomalies.find((anomaly: Anomaly) => anomaly.uuid === id);
    if (selectedAnomaly) {
      selectedAnomaly.isResolved = isResolved;
    }
    setAnomalyList(anomalies);
  };

  const onSelectCategories = (category: AnomalyCategory | string) => {
    const anomalyListCopy = [...ANOMALY_DATA_LIST];
    const filteredAnomalies =
      category !== 'ALL'
        ? anomalyListCopy.filter((anomaly: Anomaly) => anomaly.category === category)
        : ANOMALY_DATA_LIST;
    setAnomalyList(filteredAnomalies);
  };

  return (
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
      <Box className={classes.troubleshootAdvisorBox} mt={3}>
        {anomalyList.map((rec: any) => (
          <>
            <PrimaryDashboard
              key={rec.uuid}
              idKey={rec.uuid}
              uuid={rec.uuid}
              category={rec.category}
              data={rec}
              resolved={!!rec.isResolved}
              onResolve={handleResolve}
              universeUuid={rec.universeUuid}
              appName={appName}
              baseUrl={baseUrl}
              timezone={timezone}
            />
          </>
        ))}
      </Box>
    </Box>
  );
};
