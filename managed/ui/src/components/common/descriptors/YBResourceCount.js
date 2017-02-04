// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

import './stylesheets/YBResourceCount.scss'

export default class YBResourceCount extends Component {
  static propTypes = {
    kind: PropTypes.string.isRequired,
    unit: PropTypes.string,
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
    const {size, kind, unit, pluralizeKind, pluralizeUnit} = this.props;
    var displayUnit = unit && pluralizeUnit ?
      ((size === 1) ? unit : (this.props.unitPlural || this.pluralize(unit))) :
      unit;
    var displayKind = kind && pluralizeKind ?
      ((size === 1) ? kind : (this.props.kindPlural || this.pluralize(kind))) :
      kind;

    return (
      <div className="quantity">
        <div className="quantity-size">
          {size}
          {displayUnit && <span className="quantity-unit">{displayUnit}</span>}
        </div>
        <div className="quantity-kind">{displayKind}</div>
      </div>
    );
  }
}
