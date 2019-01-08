// Copyright (c) YugaByte, Inc.

import React, { PureComponent } from 'react';
import PropTypes from 'prop-types';

import './stylesheets/YBResourceCount.scss';

export default class YBResourceCount extends PureComponent {
  static propTypes = {
    kind: PropTypes.string,
    unit: PropTypes.string,
    className: PropTypes.string,
    pluralizeKind: PropTypes.bool,
    pluralizeUnit: PropTypes.bool,
    typePlural: PropTypes.string,
    unitPlural: PropTypes.string,
  };
  static defaultProps = {
    pluralizeKind: false,
    pluralizeUnit: false,
  };

  pluralize(unit) {
    return unit + (unit.match(/s$/) ? 'es' : 's');
  }

  render() {
    const {size, kind, unit, pluralizeKind, className, pluralizeUnit} = this.props;
    const displayUnit = unit && pluralizeUnit ?
      ((size === 1) ? unit : (this.props.unitPlural || this.pluralize(unit))) :
      unit;
    const displayKind = kind && pluralizeKind ?
      ((size === 1) ? kind : (this.props.kindPlural || this.pluralize(kind))) :
      kind;

    return (
      <div className={"yb-resource-count "+className}>
        <div className="yb-resource-count-size">
          {size}
          {displayUnit && <span className="yb-resource-count-unit">{displayUnit}</span>}
        </div>
        {kind && <div className="yb-resource-count-kind">{displayKind}</div>}
      </div>
    );
  }
}
