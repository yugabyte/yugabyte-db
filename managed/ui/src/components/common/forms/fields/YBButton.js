// Copyright (c) YugaByte, Inc.

import React, { PureComponent, Fragment } from 'react';
import { Button } from 'react-bootstrap';
import { YBLoadingLinearIcon } from '../../indicators';
export default class YBButton extends PureComponent {

  render() {
    const {btnClass, btnText, btnIcon, btnSize, btnType, btnStyle, disabled, loading, ...otherProps} = this.props;
    return (
      <Button bsClass={btnClass} type={btnType}
        onClick={this.props.onClick} bsSize={btnSize}
        bsStyle={btnStyle} disabled={disabled}
        {...otherProps}
      >
        {
          loading
          ?
            <YBLoadingLinearIcon />
          :
            <Fragment>
              <i className={btnIcon}></i>
              {btnText}
            </Fragment>
        }

      </Button>
    );
  }
}
