import { FC } from 'react';
import { ButtonGroup, Button } from 'react-bootstrap';

import {
  MIN_OUTLIER_NUM_NODES,
  MAX_OUTLIER_NUM_NODES,
  MAX_OUTLIER_NUM_TABLES,
  SplitType
} from '../../metrics/constants';
import { YBControlledNumericInput } from '../../common/forms/fields';
import treeIcon from '../../metrics/images/tree-icon.svg';
import './OutlierSelector.scss';

interface OutlierSelectorData {
  setNumNodeValue: any;
  onOutlierTypeChanged: any;
  defaultOutlierNumNodes: number;
  selectedOutlierType: string;
  outlierTypes: [{ value: string; label: string }];
  splitType: string;
  isK8Universe: boolean;
}

export const OutlierSelector: FC<OutlierSelectorData> = ({
  outlierTypes,
  selectedOutlierType,
  onOutlierTypeChanged,
  setNumNodeValue,
  defaultOutlierNumNodes,
  splitType,
  isK8Universe
}) => {
  const inputFormat = (num: Number) => {
    return splitType === SplitType.NODE
      ? num + (isK8Universe ? ' pods' : ' nodes')
      : num + ' tables';
  };

  return (
    <div className="outlier-container">
      <img className="downright-arrow" src={treeIcon} alt="Indicator towards Top K outlier nodes" />
      <span className="outlier-content">
        <span className="outlier-display-label">{'Display the'}</span>
        <ButtonGroup>
          {outlierTypes.map((outlierType: any, idx: number) => {
            return (
              <Button
                key={idx}
                className={`outlier-button__${outlierType.label.toLowerCase()}`}
                onClick={() => onOutlierTypeChanged(outlierType.value)}
                active={selectedOutlierType === outlierType.value}
              >
                <span className="outlier-button-title">{outlierType.label}</span>
              </Button>
            );
          })}
        </ButtonGroup>
      </span>
      <YBControlledNumericInput
        name="num-nodes"
        className="outlier-num-nodes"
        val={defaultOutlierNumNodes}
        minVal={MIN_OUTLIER_NUM_NODES}
        maxVal={splitType === SplitType.NODE ? MAX_OUTLIER_NUM_NODES : MAX_OUTLIER_NUM_TABLES}
        valueFormat={inputFormat}
        onInputChanged={(numNodes: any) => setNumNodeValue(numNodes)}
      />
    </div>
  );
};
