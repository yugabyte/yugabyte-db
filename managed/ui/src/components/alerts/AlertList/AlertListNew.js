import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBCheckBox } from '../../common/forms/fields';
import AlertsTable from './AlertsTable';
import Select from 'react-select';

import { RbacValidator } from '../../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../../redesign/features/rbac/UserPermPathMapping';
import './AlertListNew.scss';

/**
 * Whenever you change the keys of FILTER_TYPES, make sure you change it in the Alertable's useEffect to reset the page count.
 */

const STATUS_FILTER_KEY = 'states';
const SEVERITY_FILTER_KEY = 'severities';
const GROUP_TYPE_FILTER_KEY = 'configurationTypes';
const SOURCE_NAME_FILTER_KEY = 'sourceName';

const FILTER_TYPES = {
  [STATUS_FILTER_KEY]: {
    label: 'Status',
    values: ['Active', 'Acknowledged', 'Resolved']
  },

  [SEVERITY_FILTER_KEY]: {
    label: 'Severity',
    values: ['Severe', 'Warning']
  },

  [GROUP_TYPE_FILTER_KEY]: {
    label: 'Group Type',
    values: ['Platform', 'Universe']
  }
};

const REACT_FILTER_MENU_STYLES = {
  background: '#FFF',
  width: 300,
  zIndex: '999'
};

const DEFAULT_FILTERS = {
  [STATUS_FILTER_KEY]: ['ACTIVE', 'ACKNOWLEDGED'],
  [SEVERITY_FILTER_KEY]: ['SEVERE']
};

const getFilterCheckbox = (filter_text, onClickFn, isChecked) => (
  <YBCheckBox
    input={{ autoComplete: 'off' }}
    checkState={isChecked}
    onClick={onClickFn}
    label={<span className="checkbox-label">{filter_text}</span>}
  />
);

export class AlertListNew extends Component {
  state = {
    filter_groups: {
      ...DEFAULT_FILTERS
    },
    universesList: []
  };

  componentDidMount() {
    this.props.fetchUniverseList().then((data) => {
      const universesList = [
        ...data?.map((universe) => {
          return {
            value: universe.name,
            label: universe.name
          };
        }) ?? []
      ];

      this.setState({
        universesList
      });
    });
  }

  updateSourceName = (selectedValue) => {
    const { filter_groups } = this.state;
    const { value } = selectedValue;

    filter_groups[SOURCE_NAME_FILTER_KEY] = value;
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

    /**
     * Remove universe from filters , if universe is unselected
     */

    if (
      filter_groups[GROUP_TYPE_FILTER_KEY] !== undefined &&
      filter_groups[GROUP_TYPE_FILTER_KEY].indexOf('UNIVERSE') === -1
    ) {
      filter_groups[SOURCE_NAME_FILTER_KEY] = undefined;
    }

    this.setState({
      filter_groups
    });
  };

  isSelected = (groupType, filter_key) => {
    const { filter_groups } = this.state;
    if (!filter_groups[groupType]) {
      return false;
    }
    return filter_groups[groupType].indexOf(filter_key.toUpperCase()) !== -1;
  };

  render() {
    const { filter_groups, universesList } = this.state;
    const { customer } = this.props;
    return (
      <div>
        <h2 className="content-title">Alerts</h2>
        <RbacValidator
          accessRequiredOn={{
            ...UserPermissionMap.readAlerts
          }}
        >
          <Row className="alerts-page">
            <Col lg={12} className="alerts-container">
              <Row>
                <Col className="alerts-filters" lg={2}>
                  <h3>Filters</h3>
                  <Row className="filter-group noPaddingLeft" key={GROUP_TYPE_FILTER_KEY}>
                    <Col lg={12} className="noPaddingLeft">
                      <h6 className="to-uppercase">{FILTER_TYPES[GROUP_TYPE_FILTER_KEY].label}</h6>
                      {FILTER_TYPES[GROUP_TYPE_FILTER_KEY]['values'].map((filter) => {
                        return (
                          <Row key={filter}>
                            <Col lg={12} lgOffset={1} className="noMargin noPaddingLeft">
                              {getFilterCheckbox(
                                filter,
                                (e) => {
                                  this.updateFilters(GROUP_TYPE_FILTER_KEY, filter, e.target.checked);
                                },
                                this.isSelected(GROUP_TYPE_FILTER_KEY, filter)
                              )}
                            </Col>
                          </Row>
                        );
                      })}
                    </Col>
                  </Row>
                  {filter_groups[GROUP_TYPE_FILTER_KEY] &&
                    filter_groups[GROUP_TYPE_FILTER_KEY].indexOf('UNIVERSE') !== -1 && (
                      <Row className="filter-group noPaddingLeft">
                        <Col lg={12} className="noPaddingLeft">
                          <h6 className="to-uppercase">Universe</h6>
                          <Row>
                            <Col lg={12} lgOffset={1} className="noMargin noPaddingLeft">
                              <Select
                                isMulti={false}
                                options={universesList}
                                onChange={this.updateSourceName}
                                menuPortalTarget={document.body}
                                styles={{
                                  menuPortal: (base) => ({ ...base, zIndex: 999 }),
                                  menu: (styles) => ({
                                    ...styles,
                                    ...REACT_FILTER_MENU_STYLES
                                  })
                                }}
                              />
                            </Col>
                          </Row>
                        </Col>
                      </Row>
                    )}
                  {[STATUS_FILTER_KEY, SEVERITY_FILTER_KEY].map((filter_key) => (
                    <Row className="filter-group noPaddingLeft" key={filter_key}>
                      <Col lg={12} className="noPaddingLeft">
                        <h6 className="to-uppercase">{FILTER_TYPES[filter_key].label}</h6>
                        {FILTER_TYPES[filter_key]['values'].map((filter) => {
                          return (
                            <Row key={filter}>
                              <Col lg={12} lgOffset={1} className="noMargin noPaddingLeft">
                                {getFilterCheckbox(
                                  filter,
                                  (e) => {
                                    this.updateFilters(filter_key, filter, e.target.checked);
                                  },
                                  this.isSelected(filter_key, filter)
                                )}
                              </Col>
                            </Row>
                          );
                        })}
                      </Col>
                    </Row>
                  ))}
                </Col>
                <Col lg={10}>
                  <AlertsTable filters={filter_groups} customer={customer} />
                </Col>
              </Row>
            </Col>
          </Row>
        </RbacValidator>
      </div>
    );
  }
}
