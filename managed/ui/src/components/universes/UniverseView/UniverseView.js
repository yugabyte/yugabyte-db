// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { ListGroup } from 'react-bootstrap';
import moment from 'moment';
import _ from 'lodash';

import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { getClusterProviderUUIDs, getProviderMetadata } from '../../../utils/UniverseUtils';
import { showOrRedirect } from '../../../utils/LayoutUtils';
import { QuerySearchInput } from '../../queries/QuerySearchInput';
import { filterBySearchTokens } from '../../queries/helpers/queriesHelper';
import { getUniversePendingTask, getUniverseStatus } from '../helpers/universeHelpers';
import { YBControlledSelectWithLabel } from '../../common/forms/fields';
import { YBUniverseItem } from '..';

import 'react-bootstrap-table/css/react-bootstrap-table.css';
import './UniverseView.scss';

const dropdownFieldKeys = {
  'Universe Name': {
    value: 'name',
    type: 'string'
  },
  'Provider Type': {
    value: 'providerTypes',
    type: 'stringArray'
  },
  'Provider Name': {
    value: 'providerNames',
    type: 'stringArray'
  },
  'Price per Month': {
    value: 'pricePerMonth',
    type: 'number'
  },
  'Creation Date': {
    value: 'creationDate',
    type: 'timestamp'
  },
  Status: {
    value: 'status',
    type: 'string'
  }
};

export class UniverseView extends Component {
  constructor(props) {
    super();
    this.state = {
      searchTokens: [],
      sortField: 'Universe Name',
      sortOrder: 'Ascending',
      universePendingTasks: {}
    };
  }

  componentDidMount() {
    this.props.fetchUniverseMetadata();
    this.props.fetchUniverseTasks();
  }

  componentDidUpdate(prevProps) {
    if (
      !_.isEqual(this.props.universe.universeList, prevProps.universe.universeList) ||
      (this.props.universe.universeList &&
        !_.isEqual(this.props.tasks.customerTaskList, prevProps.tasks.customerTaskList))
    ) {
      this.updateUniversePendingTasks(this.props.universe.universeList.data);
    }
  }

  componentWillUnmount() {
    this.props.resetUniverseTasks();
  }

  handleSearchTokenChange = (newTokens) => {
    this.setState({ searchTokens: newTokens });
  };

  handleSortFieldChange = (event) => {
    this.setState({ sortField: event.target.value });
  };

  handleSortOrderChange = (event) => {
    this.setState({ sortOrder: event.target.value });
  };

  updateUniversePendingTasks(universes) {
    const { tasks } = this.props;
    const newUniversePendingTasks = {};
    universes.forEach((universe) => {
      const universePendingTask = getUniversePendingTask(
        universe.universeUUID,
        tasks.customerTaskList
      );
      newUniversePendingTasks[universe.universeUUID] = universePendingTask;
    });
    this.setState({ universePendingTasks: newUniversePendingTasks });
  }

  universeSortFunction = (a, b) => {
    const { sortField, sortOrder } = this.state;
    let order = 0;
    if (sortField === 'Creation Date') {
      order = Date.parse(a.creationDate) - Date.parse(b.creationDate);
    }
    // Break ties with universe name in ascending order
    const sortKey = dropdownFieldKeys[sortField].value;
    if (a[sortKey] === b[sortKey]) {
      return a.name < b.name ? -1 : 1;
    }

    order = a[sortKey] < b[sortKey] ? -1 : 1;
    return sortOrder === 'Ascending' ? order : order * -1;
  };

  render() {
    const self = this;
    const {
      universe: { universeList },
      customer: { currentCustomer }
    } = this.props;
    const { sortField, sortOrder, universePendingTasks } = this.state;
    showOrRedirect(currentCustomer.data.features, 'menu.universes');

    if (!_.isObject(universeList) || !isNonEmptyArray(universeList.data)) {
      return <h5>No universes defined.</h5>;
    }
    let universes = universeList.data.map((universeBase) => {
      let universe = _.cloneDeep(universeBase);
      universe.pricePerMonth = universe.pricePerHour * 24 * moment().daysInMonth();

      const clusterProviderUUIDs = getClusterProviderUUIDs(universe.universeDetails.clusters);
      const clusterProviders = this.props.providers.data.filter((p) =>
        clusterProviderUUIDs.includes(p.uuid)
      );
      universe.providerTypes = clusterProviders.map((provider) => {
        return getProviderMetadata(provider).name;
      });
      universe.providerNames = clusterProviders.map((provider) => provider.name);

      const universeStatus = getUniverseStatus(
        universe,
        universePendingTasks[universe.universeUUID]
      );
      universe.status = universeStatus.statusText;
      return universe;
    });

    universes = filterBySearchTokens(universes, this.state.searchTokens, dropdownFieldKeys);
    const universeRowItem = universes.sort(this.universeSortFunction).map((item, idx) => {
      return <YBUniverseItem {...self.props} key={item.universeUUID} idx={idx} universe={item} />;
    });

    return (
      <React.Fragment>
        <div className="universe-list-table-toolbar-container">
          <YBControlledSelectWithLabel
            label="Sort Field"
            options={Object.keys(dropdownFieldKeys)
              .filter((field) => dropdownFieldKeys[field].type !== 'stringArray')
              .map((field, idx) => (
                <option key={idx} id={idx}>
                  {field}
                </option>
              ))}
            selectVal={sortField}
            onInputChanged={this.handleSortFieldChange}
          />
          <YBControlledSelectWithLabel
            label="Sort Order"
            options={['Ascending', 'Descending'].map((field, idx) => (
              <option key={idx} id={idx}>
                {field}
              </option>
            ))}
            selectVal={sortOrder}
            onInputChanged={this.handleSortOrderChange}
          />
          <div className="universe-list-table-toolbar-button-container">{this.props.children}</div>
        </div>
        <QuerySearchInput
          id="universe-list-search-bar"
          columns={dropdownFieldKeys}
          placeholder="Filter by universe details"
          searchTerms={this.state.searchTokens}
          onSubmitSearchTerms={this.handleSearchTokenChange}
        />
        <ListGroup>{universeRowItem}</ListGroup>
      </React.Fragment>
    );
  }
}
