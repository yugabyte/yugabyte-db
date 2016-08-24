// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import RegionMap from './RegionMap';

export default class UniverseDetail extends Component {
  static contextTypes = {
    router: PropTypes.object
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
  }
  
  componentDidMount() {
    this.props.getUniverseInfo(this.props.uuid);
  }

  render() {
    const { currentUniverse, loading } = this.props.universe;
    if (loading) {
      return <div className="container">Loading...</div>;
    } else if (!currentUniverse) {
      return <span />;
    }

    return (
      <div id="page-wrapper">
        <div className="row header-row">
          <div className="col-lg-10">
            <h3>UniverseDetail: { currentUniverse.name } </h3>
          </div>
        </div>
        <div className="row">
          <RegionMap regions={currentUniverse.regions}/>
        </div>
      </div>);
  }
}
