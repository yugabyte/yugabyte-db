// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

export default class YBButton extends Component {

  constructor(props) {
    super(props);
    this.btnClicked = this.btnClicked.bind(this);
  }
  
  btnClicked(e) {
    if (typeof this.props.onClick === 'function') {
      this.props.onClick(e.target.value);
    }
  }
  render() {
    const {btnClass, btnText, btnIcon} = this.props;
    return (
      <div className={btnClass} onClick={this.btnClicked}>
        <i className={btnIcon}></i>&nbsp;
        {btnText}
      </div>
    )
  }
}
