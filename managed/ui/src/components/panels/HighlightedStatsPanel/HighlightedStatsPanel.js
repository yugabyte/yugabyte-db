// Copyright (c) YugabyteDB, Inc.

import { Component } from 'react';
import moment from 'moment';
import { YBFormattedNumber, YBResourceCount } from '../../common/descriptors';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBLoading } from '../../common/indicators';

import './HighlightedStatsPanel.scss';
import { isDefinedNotNull, isNonEmptyObject } from '../../../utils/ObjectUtils';
import { getUniverseNodeCount } from '../../../utils/UniverseUtils';
import { isAvailable } from '../../../utils/LayoutUtils';

export default class HighlightedStatsPanel extends Component {
  render() {
    const {
      universe: { universeList },
      customer: { currentCustomer }
    } = this.props;
    let numNodes = 0;
    let totalCost = 0;
    let numOfCores = 0;
    if (getPromiseState(universeList).isLoading()) {
      return <YBLoading />;
    }
    if (!(getPromiseState(universeList).isSuccess() || getPromiseState(universeList).isEmpty())) {
      return <span />;
    }

    if (universeList.data) {
      universeList.data.forEach(function (universeItem) {
        if (isNonEmptyObject(universeItem.universeDetails)) {
          numNodes += getUniverseNodeCount(universeItem.universeDetails.nodeDetailsSet);
          numOfCores += universeItem.resources.numCores;
        }
        if (isDefinedNotNull(universeItem.pricePerHour)) {
          totalCost += universeItem.pricePerHour * 24 * moment().daysInMonth();
        }
      });
    }
    const formattedCost = (
      <YBFormattedNumber
        value={totalCost}
        maximumFractionDigits={2}
        formattedNumberStyle="currency"
        currency="USD"
      />
    );
    return (
      <div className="tile_count highlighted-stats-panel">
        <YBResourceCount
          kind="Universes"
          size={isDefinedNotNull(universeList.data) ? universeList.data.length : 0}
        />
        <YBResourceCount kind="Nodes" size={numNodes} />
        <YBResourceCount kind="Cores" size={numOfCores} />
        {isAvailable(currentCustomer.data.features, 'costs.stats_panel') && (
          <YBResourceCount kind="Per Month" size={formattedCost} />
        )}
      </div>
    );
  }
}
