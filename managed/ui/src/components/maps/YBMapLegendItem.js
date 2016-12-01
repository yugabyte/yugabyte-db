// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Col, Image, ListGroup, ListGroupItem} from 'react-bootstrap';

import { YBButton } from '../common/forms/fields';
import { RootMarkerIcon, AsyncMarkerIcon, CacheMarkerIcon } from './images';

export default class YBMapLegendItem extends Component {
  render() {
    const {regions, title, type} = this.props;
    var btnItem = <span/>;
    var legendItemImg = "";
    if (type === "Root") {
      legendItemImg = RootMarkerIcon;
      btnItem = <YBButton btnClass="btn-block bg-orange" btnText="Add"/>;
    } else if( type === "Cache") {
      legendItemImg = AsyncMarkerIcon;
    } else if (type === "Async") {
      legendItemImg = CacheMarkerIcon;
    }
    return (
      <div>
        <Col lg={9}>
           <span><Image src={legendItemImg} className="legend-img"/>
             <h2 className="inline-display">{title}</h2>
           </span>
        </Col>
        <Col lg={3}>
          {btnItem}
        </Col>
        <Col lg={12}>
          <ListGroup>
            {
              regions.map(function(item, idx){
                return <ListGroupItem key={item+idx}>{item.name}</ListGroupItem>
              })
            }
          </ListGroup>
        </Col>
      </div>
    )
  }
}

YBMapLegendItem.propTypes = {
  regions: React.PropTypes.array.isRequired,
  title: React.PropTypes.string.isRequired,
  type: PropTypes.oneOf(['Root', 'Async', 'Cache'])
}
