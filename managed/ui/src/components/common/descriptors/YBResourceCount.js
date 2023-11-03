// Copyright (c) YugaByte, Inc.

import { PureComponent } from 'react';
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
    separatorLine: PropTypes.bool,
    icon: PropTypes.string,
    sizePrefix: PropTypes.string
  };
  static defaultProps = {
    pluralizeKind: false,
    pluralizeUnit: false,
    separatorLine: false,
    icon: '',
    sizePrefix: ''
  };

  pluralize(unit) {
    return unit + (unit.match(/s$/) ? 'es' : 's');
  }

  render() {
    const {
      size,
      kind,
      unit,
      inline,
      label,
      pluralizeKind,
      className,
      pluralizeUnit,
      separatorLine,
      icon,
      sizePrefix
    } = this.props;
    const displayUnit =
      unit && pluralizeUnit
        ? size === 1
          ? unit
          : this.props.unitPlural || this.pluralize(unit)
        : unit;
    const displayKind =
      kind && pluralizeKind
        ? size === 1
          ? kind
          : this.props.kindPlural || this.pluralize(kind)
        : kind;
    const classNames = (inline ? 'yb-resource-count-inline ' : null) + className;

    return (
      <div className={'yb-resource-count ' + classNames}>
        {label && <div className="yb-resource-count-label">{label}</div>}
        <div className="yb-resource-count-size">
          {sizePrefix && <span className="yb-resource-count-size-prefix">{sizePrefix}</span>}
          {size}
          {kind && inline && <div className="yb-resource-count-kind">{displayKind}</div>}
          {displayUnit && <span className="yb-resource-count-unit">{displayUnit}</span>}
        </div>
        {separatorLine && <div className="yb-resource-separator-line" />}
        {kind && !inline && (
          <div className="yb-resource-count-kind">
            {icon && <i className={`fa ${icon}`} aria-hidden="true" />}
            {displayKind}
          </div>
        )}
      </div>
    );
  }
}
