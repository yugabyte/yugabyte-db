// Copyright (c) YugaByte, Inc.

import { PureComponent, Fragment } from 'react';
import { Button } from 'react-bootstrap';
import clsx from 'clsx';

import { YBLoadingLinearIcon } from '../../indicators';
import { isDefinedNotNull } from '../../../../utils/ObjectUtils';

export default class YBButton extends PureComponent {
  render() {
    const {
      btnClass,
      btnText,
      btnIcon,
      btnSize,
      btnType,
      btnStyle,
      disabled,
      loading,
      className,
      ...otherProps
    } = this.props;
    const btnClassName = isDefinedNotNull(btnText) ? btnIcon : `${btnIcon} no-margin no-padding`;
    return (
      <Button
        bsClass={btnClass}
        type={btnType}
        onClick={this.props.onClick}
        bsSize={btnSize}
        bsStyle={btnStyle}
        disabled={!!disabled}
        className={clsx(className)}
        {...otherProps}
      >
        {loading ? (
          <YBLoadingLinearIcon />
        ) : (
          <Fragment>
            {btnClassName && <i className={btnClassName}></i>}
            {btnText}
          </Fragment>
        )}
      </Button>
    );
  }
}
