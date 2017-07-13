// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Image, ListGroup, ListGroupItem} from 'react-bootstrap';

import { YBButton } from '../common/forms/fields';
import { RootMarkerIcon, AsyncMarkerIcon, CacheMarkerIcon } from './images';

import './stylesheets/YBMapLegendItem.scss'

export default class YBMapLegendItem extends Component {
  render() {
    const {regions, title, type} = this.props;
    var btnItem = <span/>;
    var legendItemImg = "";
    if (type === "Root") {
      legendItemImg = RootMarkerIcon;
      btnItem = <YBButton btnText="Add"/>;
    } else if( type === "Cache") {
      legendItemImg = CacheMarkerIcon;
    } else if (type === "Async") {
      legendItemImg = AsyncMarkerIcon;
    }
    return (
      <div className="map-legend-item">
        <div className="float-right">
          {btnItem}
        </div>
        <div className="icon-hang-left"><Image src={legendItemImg} className="legend-img"/></div>
        <h5 className="map-legend-heading">{title}</h5>
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
  regions: PropTypes.array.isRequired,
  title: PropTypes.string.isRequired,
  type: PropTypes.oneOf(['Root', 'Async', 'Cache'])
}
