// Copyright (c) YugaByte, Inc.

import { PureComponent, Fragment } from 'react';
import { YBLoadingLinearIcon } from '../../indicators';
import { isDefinedNotNull } from '../../../../utils/ObjectUtils';

export default class YBButtonLink extends PureComponent {
  render() {
    const {
      btnClass,
      btnText,
      btnIcon,
      link,
      btnStyle,
      disabled,
      onClick,
      loading,
      ...otherProps
    } = this.props;
    const className = isDefinedNotNull(btnText) ? btnIcon : `${btnIcon} no-margin no-padding`;
    return (
      <a
        href={link}
        className={btnClass}
        onClick={onClick}
        style={btnStyle}
        disabled={disabled}
        {...otherProps}
      >
        {loading ? (
          <YBLoadingLinearIcon />
        ) : (
          <Fragment>
            <i className={className}></i>
            {btnText}
          </Fragment>
        )}
      </a>
    );
  }
}
