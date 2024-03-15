import { useState } from 'react';
import { Box, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { MetricMeasure } from '../utils/dtos';
import { YBButton } from '@yugabytedb/ui-components';
import TreeIcon from '../assets/tree-icon.svg';

interface MetricSplitSelectorProps {
  metricSplitSelectors: any;
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
  onSplitTypeSelected
}: MetricSplitSelectorProps) => {
  const classes = useStyles();
  const [active, setActive] = useState(0);

  return (
    <Box ml={2} display="flex" flexDirection="row">
      <Box display="flex" flexDirection="row">
        <img
          className={classes.icon}
          src={TreeIcon}
          alt="Indicator towards metric measure to use"
        />
        {/* <YBLabel width="120px">{"View metrics for"}</YBLabel> */}
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
                onSplitTypeSelected(metricSplitSelector.label);
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
