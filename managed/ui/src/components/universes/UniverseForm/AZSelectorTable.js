import React, { Component, PropTypes } from 'react';
import { Field } from 'redux-form';
import { YBNewSelect, YBNewNumericInput } from 'components/common/forms/fields';
import { isValidArray, sortedGroupCounts } from 'utils/ObjectUtils';

export default class AZSelectorTable extends Component {
  static propTypes = {
    universe: PropTypes.object,
  }

  render() {
    const { universe } = this.props;

    if (!isValidArray(universe.universeResourceTemplate.azList)) {
      return <span />;
    }

    var maxNodeCount = universe.universeResourceTemplate.azList.length;
    var counts = sortedGroupCounts(universe.universeResourceTemplate.azList);
    var zoneNames = counts.map(function(zone) { return zone.value; });

    var azFieldRows = counts.map(function(zone, idx) {
      var rowName = 'az_' + idx;

      // TODO: Add zone name (zone.value) and node count (zone.count) to form state
      // TODO: to populate the default values of the fields below.

      var zoneOptions = zoneNames.map(function(item, idx) {
        return (<option key={'az_select_option_' + idx} value={item} selected={item === zone.value}>{item}</option>);
      });

      return (
        <tr key={rowName}>
          <td>
            <Field name={rowName + '_name'} type="select" component={YBNewSelect} options={zoneOptions} />
          </td>
          <td>
            <Field name={rowName + '_node_count'} type="text" component={YBNewNumericInput}
              minVal={1} maxVal={maxNodeCount} />
          </td>
        </tr>
      );
    });

    return (
      <table className="form-grid">
        <thead>
          <tr>
            <th>Name</th>
            <th>Nodes</th>
          </tr>
        </thead>
        <tbody>
          {azFieldRows}
        </tbody>
      </table>
    );
  }
}
