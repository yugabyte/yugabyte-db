// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Image, ListGroup, ListGroupItem} from 'react-bootstrap';
import { RootMarkerIcon, AsyncMarkerIcon, CacheMarkerIcon } from './images';
import './stylesheets/YBMapLegendItem.scss';

export default class YBMapLegendItem extends Component {
  render() {
    const {regions, title, type} = this.props;
    var legendItemIcon = "";
    if (type === "Root") {
      legendItemIcon = <Image src={RootMarkerIcon} className="legend-img"/>;
    } else if( type === "Cache") {
      legendItemIcon = <Image src={CacheMarkerIcon} className="legend-img"/>;
    } else if (type === "Async") {
      legendItemIcon = <Image src={AsyncMarkerIcon} className="legend-img"/>;
    } else if (type === "Region") {
      legendItemIcon = <div className="marker-cluster-small provider-marker-cluster">#</div>;
    }
    console.log(type);
    let mapLegendDetails = <span/>;
    if (type !== "Region") {
      mapLegendDetails = (
        <ListGroup>
          {
            regions.map(function(item, idx){
              return <ListGroupItem key={item+idx}>{item.name}</ListGroupItem>;
            })
          }
        </ListGroup>
      );
    }
    return (
      <div className="map-legend-item">
        <div className="icon-hang-left">
          {legendItemIcon}
        </div>
        <h5 className="map-legend-heading">{title}</h5>
        {mapLegendDetails}
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
