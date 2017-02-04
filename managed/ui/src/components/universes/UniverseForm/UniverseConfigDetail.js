import React, { Component } from 'react';
import { isDefinedNotNull } from 'utils/ObjectUtils';
import { YBResourceCount, YBCost } from 'components/common/descriptors';

export default class UniverseConfigDetail extends Component {
  render() {
    const {universe: {universeResourceTemplate}} =  this.props;
    var empty = false;
    var costPerDay = '$0.00';
    var costPerMonth = '$0.00';
    if (isDefinedNotNull(universeResourceTemplate) && Object.keys(universeResourceTemplate).length > 0) {
      costPerDay = <YBCost value={universeResourceTemplate.pricePerHour} multiplier={"day"} />
      costPerMonth = <YBCost value={universeResourceTemplate.pricePerHour} multiplier={"month"} />
    } else {
      empty = !(universeResourceTemplate.numCores || universeResourceTemplate.memSizeGB ||
        universeResourceTemplate.volumeSizeGB || universeResourceTemplate.volumeCount);
    }
    return (
      <div className={"universe-resource-preview " + (empty ? 'empty' : '')}>
        <YBResourceCount size={universeResourceTemplate.numCores || 0} kind="Core" pluralizeKind />
        <YBResourceCount size={universeResourceTemplate.memSizeGB || 0} unit="GB" kind="Memory" />
        <YBResourceCount size={universeResourceTemplate.volumeSizeGB || 0} unit="GB" kind="Storage" />
        <YBResourceCount size={universeResourceTemplate.volumeCount || 0} kind="Volume" pluralizeKind />
        <YBResourceCount size={costPerDay} kind="/day" />
        <YBResourceCount size={costPerMonth} kind="/month" />
      </div>
    )
  }
}
