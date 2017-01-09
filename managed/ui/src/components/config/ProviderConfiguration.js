// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { withRouter } from 'react-router';

class ProviderConfiguration extends Component {
  constructor(props) {
    super(props);
    this.parentRoute = this.parentRoute.bind(this);
  }
  parentRoute() {
    this.props.router.push("/config");
  }
}

export default withRouter(ProviderConfiguration);
