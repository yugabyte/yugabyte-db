// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import logo from './images/small-logo.png';
import { Image } from 'react-bootstrap';

export default class YBLogo extends Component {
  render() {
    return <Image src={logo} className="yb-logo-img" />;
  }
}
