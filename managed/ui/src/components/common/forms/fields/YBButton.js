// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Button} from 'react-bootstrap';
export default class YBButton extends Component {

  render() {
    const {btnClass, btnText, btnIcon, btnSize, btnType, btnStyle, disabled} = this.props;
    return (
      <Button bsClass={btnClass} type={btnType}
              onClick={this.props.onClick} bsSize={btnSize} bsStyle={btnStyle} disabled={disabled}>
        <i className={btnIcon}></i> &nbsp;
        {btnText}
      </Button>
    )
  }
}
