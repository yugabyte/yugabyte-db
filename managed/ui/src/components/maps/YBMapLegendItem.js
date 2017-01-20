// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Image, ListGroup, ListGroupItem} from 'react-bootstrap';

import { YBButton } from '../common/forms/fields';
import { RootMarkerIcon, AsyncMarkerIcon, CacheMarkerIcon } from './images';

import './stylesheets/YBMapLegendItem.css'

export default class YBMapLegendItem extends Component {
  render() {
    const {regions, title, type} = this.props;
    var btnItem = <span/>;
    var legendItemImg = "";
    if (type === "Root") {
      legendItemImg = RootMarkerIcon;
      btnItem = <YBButton btnText="Add"/>;
    } else if( type === "Cache") {
      legendItemImg = AsyncMarkerIcon;
    } else if (type === "Async") {
      legendItemImg = CacheMarkerIcon;
    }
    return (
      <div className="map-legend-item">
        <div className="float-right">
          {btnItem}
        </div>
        <div className="icon-hang-left"><Image src={legendItemImg} className="legend-img"/></div>
        <h5>{title}</h5>
        <ListGroup>
          {
            regions.map(function(item, idx){
              return <ListGroupItem key={item+idx}>{item.name}</ListGroupItem>
            })
          }
        </ListGroup>
      </div>
    )
  }
}

YBMapLegendItem.propTypes = {
  regions: React.PropTypes.array.isRequired,
  title: React.PropTypes.string.isRequired,
  type: PropTypes.oneOf(['Root', 'Async', 'Cache'])
}
