// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Link } from 'react-router';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { FormattedDate } from 'react-intl';
import { isValidArray, isValidObject } from '../utils/ObjectUtils';
import TaskProgressContainer from '../containers/TaskProgressContainer';
import { Row, Col, Image } from 'react-bootstrap';
import universelogo from '../images/universe_icon.png';
import DescriptionItem from './DescriptionItem';
import DescriptionList from './DescriptionList';

class StatusStringCell extends Component {
 render() {
   const {status, tasks} = this.props;
   const stringHeader = <div className='table-cell-sub-text text-center'> Status </div>;
   var statusInfo = "";
   if (status === "success") {
     statusInfo = <i className='fa fa-check fa-fw' aria-hidden='true'> Succeeded </i>;
   } else if (status === "failure") {
     statusInfo = <i className='fa fa-times fa-fw' > Failed </i>
   }

   if(status === "failure" || status === "success" ) {
     return (
       <Row>
         <Col lg={8} lgOffset={2}>
           <DescriptionItem title={stringHeader}>
             <div className="text-center">{statusInfo}</div>
           </DescriptionItem>
         </Col>
       </Row>
     )
    }
   else if (isValidArray(tasks)){
     var taskIds = tasks.map(function(item, idx){
       return (item.id);
     })
     var currentOp = tasks[0].data.title.split(":")[0];
     return (
       <Row>
         <Col lg={12} className="universe-table-status-cell">
           <DescriptionItem title={stringHeader}>
             <TaskProgressContainer taskUUIDs={taskIds} type="BarWithDetails"
                                    currentOperation={currentOp} />
           </DescriptionItem>
         </Col>
       </Row>
   )
   }
   else {
     return ( <span /> )
   }
 }
}

class UniverseNameCell extends Component {
    render() {
      const { cell } = this.props;
      return (
        <Link to={"/universes/" + cell.id}>
          <div className='universe-name-cell'>
            <Image src={universelogo} />
             &nbsp;{cell.name}&nbsp;
          </div>
          <small className="table-cell-sub-text">Created on&nbsp;
            {cell.creationTime}
          </small>
        </Link>)
    }
}

class RegionDataComponent extends Component {

  render() {
    const { regions, name } = this.props;

    var completeRegionList = regions.map(function(item,idx){
      return {"data": item.name, "dataClass": "show-data"}
    });
     return (
        <Col lg={4} className="detail-item-cell">
          <DescriptionItem title={name}>
            <DescriptionList listItems={completeRegionList} showNames={false}/>
          </DescriptionItem>
        </Col>
      )

    }
}

class DataComponents extends Component {
  render() {
    const {data, name} = this.props;
    return (
      <Col lg={2} className="detail-item-cell">
        <DescriptionItem title={name}>
          <div className="universe-item-cell">{data}</div>
        </DescriptionItem>
      </Col>
    )
  }
}

class UniverseDetailsCell extends Component {
  static propTypes = {
    cell: PropTypes.object.isRequired
  };

  render() {
    const { cell } = this.props;
    var taskUUIDs = cell["TaskUUIDs"];
    if (isValidArray(taskUUIDs)) {
      delete cell["TaskUUIDs"];
    }
    var universeDetailsItems = Object.keys(cell).map(function(key, index) {
      if(key !== "Regions") {
        return <DataComponents key={key+index} name={key} data={cell[key]}/>
      } else {
        return <RegionDataComponent key={key+index} name={key} regions={cell[key]} />
      }
    });

    return (
      <Row>
        <Col lg={1}></Col>
         {universeDetailsItems.map(function(item,idx){
           return <div key={item+idx}>{item}</div>
          })}
      </Row>
    )
  }
}

export default class UniverseTable extends Component {

  componentWillMount() {
    this.props.fetchUniverseList();
    this.props.fetchUniverseTasks();
  }

  componentWillUnmount() {
    this.props.resetUniverseList();
    this.props.resetUniverseTasks();
  }

  render() {
    var universeDisplay = [];

    function detailStringFormatter(cell, row) {
      return <UniverseDetailsCell cell={cell} />;
    }

    function universeNameTypeFormatter(cell, row) {
      return <UniverseNameCell cell={cell}/>
    }

    function statusStringFormatter(cell, row){
      return <StatusStringCell status={cell.status} tasks={cell.tasks} />
    }

    const { universe: { universeList, universeTasks, loading } } = this.props;

    if (loading) {
      return <div className="container">Loading...</div>;
    }

    if (isValidArray(universeList)) {
      universeDisplay = universeList.map(function (item, idx) {
        const { provider, regions, universeDetails } = item

        var regionNames = "";
        if (typeof(regions) !== "undefined") {
          regionNames = regions.map(function(region, idx) {
            return {"idx": idx, "name": region.name};

          });
        }
        var providerName = "";
        if (isValidObject(provider)) {
          providerName = provider.name;
        }

        var numNodes;
        var replicationFactor;
        if (isValidObject(universeDetails)) {
          numNodes = universeDetails.nodeDetailsSet.length;

          if (isValidObject(universeDetails.userIntent)) {
            replicationFactor = universeDetails.userIntent.replicationFactor;
          }
        }

        var universeTaskUUIDs = [];
        if (isValidObject(universeTasks) && universeTasks[item.universeUUID] !== undefined) {
          universeTaskUUIDs = universeTasks[item.universeUUID].map(function (task) {
            return {"id": task.id, "data": task};
          });
        }


        var universeDetailString = {
          "Provider": providerName,
          "Regions": regionNames.length > 0 ? regionNames: [],
          "Num of Nodes": numNodes,
          "Replication Factor": replicationFactor
        };

        var updateProgressStatus = false;
        var updateSuccessStatus = false;
        var status = "";
        if (isValidObject(item.universeDetails.updateInProgress)) {
          updateProgressStatus = item.universeDetails.updateInProgress;
        }
        if (isValidObject(item.universeDetails.updateSucceeded)) {
          updateSuccessStatus = item.universeDetails.updateSucceeded;
        }
        if (!updateProgressStatus && !updateSuccessStatus) {
          status = "failure";
        } else if (updateSuccessStatus) {
          status = "success";
        } else {
          status = "pending";
        }
        var statusString={"status": status, "tasks": universeTaskUUIDs};

        var formattedCreationTime = <FormattedDate value={item.creationDate}
          year='numeric' month='long' day='2-digit'/>
        var actionString = item.universeUUID;
        var universeNameObject = { "name": item.name,
                                   "creationTime": formattedCreationTime,
                                   "id": item.universeUUID};
        return {
          id: item.universeUUID,
          name: universeNameObject,
          details: universeDetailString,
          provider: providerName,
          nodes: numNodes,
          action: actionString,
          status: statusString
        };
      });
    }

    const tableHeaderStyle = {"display": "none"};
    const tableBodyStyle = {"marginBottom": "1%","paddingBottom": "1%"};

    const selectRowProp = {
      bgColor: "rgb(211,211,211)"
    };

    return (
      <div className="row">
        <BootstrapTable data={universeDisplay} selectRow={selectRowProp}
                        trClassName="data-table-row" bodyStyle={tableBodyStyle} headerStyle={tableHeaderStyle}>
          <TableHeaderColumn dataField="id" isKey={true} hidden={true}/>
          <TableHeaderColumn dataField="name"

                             dataFormat={universeNameTypeFormatter} columnClassName="no-border-cell name-column"
                             className="no-border-cell">
            Universe Name
          </TableHeaderColumn>
          <TableHeaderColumn dataField="status" dataFormat={statusStringFormatter}
                             columnClassName="no-border-cell status-column" className="no-border-cell">
            Status
          </TableHeaderColumn>
          <TableHeaderColumn dataField="details"
                             dataFormat={detailStringFormatter} columnClassName="no-border-cell detail-column">
            Details</TableHeaderColumn>
        </BootstrapTable>
      </div>
    )
  }
}
