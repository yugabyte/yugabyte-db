import { useState } from 'react';
import { Box, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { YBButton, YBLabel } from '@yugabytedb/ui-components';
import { Anomaly, MetricMeasure, SplitMode, SplitType } from '../helpers/dtos';

import treeIcon from '../assets/tree-icon.svg';

interface MetricSplitSelectorProps {
  metricSplitSelectors: any;
  anomalyData: Anomaly | null;
  onSplitTypeSelected: (selectedOption: MetricMeasure) => void;
}

const useStyles = makeStyles((theme) => ({
  icon: {
    marginRight: theme.spacing(1)
  },
  metricMeasureButton: {
    border: '1px solid #E5E5E9',
    borderRadius: '0px',
    height: '38px'
  },
  overrideMuiButton: {
    color: '#2b59c3 !important',
    background: 'linear-gradient(0deg, rgba(43, 89, 195, 0.1), rgba(43, 89, 195, 0.1)), #FFFFFF'
  },
  borderOverallButton: {
    borderRadius: '8px 0px 0px 8px'
  },
  borderOutlierButton: {
    borderRadius: '0px 8px 8px 0px'
  }
}));

export const MetricSplitSelector = ({
  metricSplitSelectors,
  onSplitTypeSelected,
  anomalyData
}: MetricSplitSelectorProps) => {
  const classes = useStyles();
  let defaultActiveIndex = 0;
  if (
    anomalyData?.defaultSettings?.splitMode === SplitMode.NONE &&
    anomalyData?.defaultSettings?.splitType === SplitType.NONE
  ) {
    defaultActiveIndex = 0;
  }

  if (
    anomalyData?.defaultSettings?.splitMode === SplitMode.TOP ||
    anomalyData?.defaultSettings?.splitMode === SplitMode.BOTTOM
  ) {
    defaultActiveIndex = anomalyData?.defaultSettings?.splitType === SplitType.NODE ? 1 : 2;
  }
  const [active, setActive] = useState(defaultActiveIndex);

  return (
    <Box ml={2} display="flex" flexDirection="row">
      <Box display="flex" flexDirection="row">
        <img
          className={classes.icon}
          src={treeIcon}
          alt="Indicator towards metric measure to use"
        />
        <YBLabel width="120px">{'View metrics for'}</YBLabel>
      </Box>
      <Box>
        {metricSplitSelectors.map((metricSplitSelector: any, idx: number) => {
          const isActive = active === idx;
          return (
            <YBButton
              className={clsx(classes.metricMeasureButton, isActive && classes.overrideMuiButton, {
                [classes.borderOverallButton]: idx === 0,
                [classes.borderOutlierButton]: idx === 2
              })}
              data-testid={`MetricTypeSelector-MetricMeasureButton`}
              onClick={() => {
                onSplitTypeSelected(metricSplitSelector.value);
                setActive(idx);
              }}
              size="medium"
              variant={'secondary'}
            >
              {metricSplitSelector.label}
            </YBButton>
          );
        })}
      </Box>
    </Box>
  );
};
