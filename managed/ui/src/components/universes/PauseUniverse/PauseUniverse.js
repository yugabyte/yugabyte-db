// Copyright (c) YugaByte, Inc.
// 
// Author: Nishant Sharma(nishant.sharma@hashedin.com)

import React, {Component} from 'react';
import {YBModal} from '../../common/forms/fields';

export default class PauseUniverse extends Component {
  constructor(props) {
    super(props);
  }

  render() {
    return(
      <YBModal></YBModal>
    );
  }
}