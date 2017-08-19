// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Image, ListGroup, ListGroupItem} from 'react-bootstrap';
import { RootMarkerIcon, AsyncMarkerIcon, CacheMarkerIcon } from './images';
import './stylesheets/YBMapLegendItem.scss';
import {isNonEmptyArray} from 'utils/ObjectUtils';

export default class YBMapLegendItem extends Component {
  render() {
    const {regions, title, type} = this.props;
    let legendItemIcon = "";
    if (type === "Root") {
      legendItemIcon = <Image src={RootMarkerIcon} className="legend-img"/>;
    } else if( type === "Cache") {
      legendItemIcon = <Image src={CacheMarkerIcon} className="legend-img"/>;
    } else if (type === "Async") {
      legendItemIcon = <Image src={AsyncMarkerIcon} className="legend-img"/>;
    } else if (type === "Region") {
      legendItemIcon = <div className="marker-cluster-small provider-marker-cluster">#</div>;
    }
    let regionList = <span/>;
    if (type !== "Region") {
      if (isNonEmptyArray(regions)) {
        regionList = regions.map(function(region, rIdx){
          if (isNonEmptyArray(region.azList)) {
            return (
              <ListGroupItem key={`${region.name}.${rIdx}`}>
                <div className="region-item-cell">{region.name}</div>
                {
                  region.azList.map(function (az, azIdx) {
                    let azNodeCount = az.numNodesInAZ === 1 ? `(${az.numNodesInAZ} Node)` : `(${az.numNodesInAZ} Nodes)`;
                    return <ListGroupItem key={az.uuid + "" + azIdx} className="az-item-cell">{`${az.name} ${azNodeCount}`}</ListGroupItem>
                  })
                }
              </ListGroupItem>
            );
          } else {
            return <ListGroupItem key={`${region.name}.${rIdx}`}>{region.name}</ListGroupItem>
          }
        })
      }
    }
    return (
      <div className="map-legend-item">
        <div className="icon-hang-left">
          {legendItemIcon}
        </div>
        <h5 className="map-legend-heading">{title}</h5>
        {regionList}
      </div>
    );
  }
}

YBMapLegendItem.defaultProps = {
  type: 'Region'
};

YBMapLegendItem.propTypes = {
  title: PropTypes.string.isRequired,
  type: PropTypes.oneOf(['Root', 'Async', 'Cache', 'Region'])
};
