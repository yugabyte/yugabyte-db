import { useState } from 'react';
import { Box, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { YBButton, YBInput, YBLabel } from '@yugabytedb/ui-components';
import { Anomaly, SplitMode } from '../helpers/dtos';

import treeIcon from '../assets/tree-icon.svg';

interface OutlierSelectorProps {
  metricOutlierSelectors: any;
  anomalyData: Anomaly;
  selectedNumNodes: number;
  onOutlierTypeSelected: (selectedOption: SplitMode) => void;
  onNumNodesChanged: (numNodes: number) => void;
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
  },
  input: {
    width: '60px',
    height: '38px'
  }
}));

export const OutlierSelector = ({
  metricOutlierSelectors,
  selectedNumNodes,
  anomalyData,
  onOutlierTypeSelected,
  onNumNodesChanged
}: OutlierSelectorProps) => {
  const classes = useStyles();

  const defaultActiveIndex = anomalyData?.defaultSettings?.splitMode === SplitMode.TOP ? 0 : 1;
  const [active, setActive] = useState(defaultActiveIndex);

  return (
    <Box ml={2} display="flex" flexDirection="row">
      <Box display="flex" flexDirection="row">
        <img
          className={classes.icon}
          src={treeIcon}
          alt="Indicator towards metric measure to use"
        />
        <YBLabel width="120px">{'Display the'}</YBLabel>
      </Box>
      <Box>
        {metricOutlierSelectors.map((outlierSelector: any, idx: number) => {
          const isActive = active === idx;
          return (
            <>
              <YBButton
                className={clsx(
                  classes.metricMeasureButton,
                  isActive && classes.overrideMuiButton,
                  {
                    [classes.borderOverallButton]: idx === 0,
                    [classes.borderOutlierButton]: idx === 1
                  }
                )}
                data-testid={`OutlierSelector-OutlierButton`}
                onClick={() => {
                  onOutlierTypeSelected(outlierSelector.value);
                  setActive(idx);
                }}
                size="medium"
                variant={'secondary'}
              >
                {outlierSelector.label}
              </YBButton>
            </>
          );
        })}
      </Box>
      <Box ml={2}>
        <YBInput
          type="number"
          className={classes.input}
          inputProps={{
            min: 1,
            'data-testid': 'OutlierSelector-NodesInput'
          }}
          value={selectedNumNodes}
          onChange={(event: any) => onNumNodesChanged(event.target.value)}
          inputMode="numeric"
        />
      </Box>
    </Box>
  );
};
