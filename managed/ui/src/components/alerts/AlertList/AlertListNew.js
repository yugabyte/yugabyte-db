import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBCheckBox, YBSelectWithLabel } from '../../common/forms/fields';
import AlertsTable from './AlertsTable';

import './AlertListNew.scss';

const FILTER_TYPES = {
  states: {
    label: 'Target State',
    values: ['Active', 'Acknowledged', 'Resolved']
  },

  severities: {
    label: 'Severity',
    values: ['Severe', 'Warning']
  },

  groupTypes: {
    label: 'Group Type',
    values: ['Customer', 'Universe']
  }
};

const getFilter = (filter_text, onClickFn) => (
  <YBCheckBox onClick={onClickFn} label={<span className="checkbox-label">{filter_text}</span>} />
);

export class AlertListNew extends Component {
  state = {
    filter_groups: {},
    universesList: []
  };

  componentDidMount() {
    this.props.fetchUniverseList().then((data) => {
      const universesList = [
        <option value="" key={'unSelected'}>
          Select an universe
        </option>,
        ...data.map((universe) => (
          <option value={universe.name} key={universe.universeUUID}>
            {universe.name}
          </option>
        ))
      ];

      this.setState({
        universesList
      });
    });
  }

  updateSourceName = (value) => {
    const { filter_groups } = this.state;
    filter_groups['sourceName'] = value;
    this.setState({
      filter_groups
    });
  };

  updateFilters = (group, filter, isChecked) => {
    const { filter_groups } = this.state;

    if (!filter_groups[group]) {
      filter_groups[group] = [];
    }

    if (isChecked) {
      filter_groups[group] = [...filter_groups[group], filter.toUpperCase()];
    } else {
      filter_groups[group] = filter_groups[group].filter((f) => f !== filter.toUpperCase());
    }

    this.setState({
      filter_groups
    });
  };

  render() {
    const { filter_groups, universesList } = this.state;

    return (
      <div>
        <h2 className="content-title">Alerts</h2>
        <Row className="alerts-page">
          <Col lg={12} className="alerts-container">
            <Row>
              <Col className="alerts-filters" lg={2}>
                <h3>Filters</h3>
                {Object.keys(FILTER_TYPES).map((filter_key) => (
                  <Row className="filter-group" key={filter_key}>
                    <Col lg={12}>
                      <h6 className="to-uppercase">{FILTER_TYPES[filter_key].label}</h6>
                      {FILTER_TYPES[filter_key]['values'].map((filter) => {
                        return (
                          <Row key={filter}>
                            <Col lg={12} lgOffset={1} className="noMargin">
                              {getFilter(filter, (e) => {
                                this.updateFilters(filter_key, filter, e.target.checked);
                              })}
                            </Col>
                          </Row>
                        );
                      })}
                    </Col>
                  </Row>
                ))}
                <Row className="filter-group">
                  <Col lg={12}>
                    <h6 className="to-uppercase">Universes</h6>
                    <Row>
                      <Col lg={12} lgOffset={1} className="noMargin">
                        <YBSelectWithLabel
                          options={universesList}
                          onInputChanged={this.updateSourceName}
                        />
                      </Col>
                    </Row>
                  </Col>
                </Row>
              </Col>
              <Col lg={10}>
                <AlertsTable filters={filter_groups} />
              </Col>
            </Row>
          </Col>
        </Row>
      </div>
    );
  }
}
