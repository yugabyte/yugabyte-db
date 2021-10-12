// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { ListGroup } from 'react-bootstrap';
import { isObject } from 'lodash';

import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { showOrRedirect } from '../../../utils/LayoutUtils';
import { YBUniverseItem } from '..';

import 'react-bootstrap-table/css/react-bootstrap-table.css';
import './UniverseTable.scss';

export default class UniverseTable extends Component {
  componentDidMount() {
    this.props.fetchUniverseMetadata();
    this.props.fetchUniverseTasks();
  }

  componentWillUnmount() {
    this.props.resetUniverseTasks();
  }

  render() {
    const self = this;
    const {
      universe: { universeList },
      universeReadWriteData,
      tasks,
      customer: { currentCustomer }
    } = this.props;
    showOrRedirect(currentCustomer.data.features, 'menu.universes');

    if (!isObject(universeList) || !isNonEmptyArray(universeList.data)) {
      return <h5>No universes defined.</h5>;
    }
    const universeRowItem = universeList.data
      .sort((a, b) => {
        return Date.parse(a.creationDate) < Date.parse(b.creationDate);
      })
      .map(function (item, idx) {
        let universeTaskUUIDs = [];
        if (isNonEmptyArray(tasks.customerTaskList)) {
          universeTaskUUIDs = tasks.customerTaskList
            .map(function (taskItem) {
              if (taskItem.targetUUID === item.universeUUID) {
                return { id: taskItem.id, data: taskItem, universe: item.universeUUID };
              } else {
                return null;
              }
            })
            .filter(Boolean)
            .sort(function (a, b) {
              return a.data.createTime < b.data.createTime;
            });
        }
        return (
          <YBUniverseItem
            {...self.props}
            key={idx}
            universe={item}
            idx={idx}
            taskId={universeTaskUUIDs}
            universeReadWriteData={universeReadWriteData}
          />
        );
      });
    return <ListGroup>{universeRowItem}</ListGroup>;
  }
}
