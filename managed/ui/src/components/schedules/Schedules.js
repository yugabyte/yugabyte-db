import React, { Component, Fragment } from 'react';
import { YBPanelItem } from '../panels';
import { Row, Col, Alert, DropdownButton, MenuItem } from 'react-bootstrap';
import { getPromiseState } from 'utils/PromiseUtils';
import moment from 'moment';
import _ from 'lodash';
import pluralize from 'pluralize';
import cronstrue from 'cronstrue';
import { DescriptionItem } from 'components/common/descriptors';
import { isNonEmptyObject, isDefinedNotNull, isNonEmptyArray } from 'utils/ObjectUtils';
import { YBModalForm } from '../common/forms';

import './schedules.scss';

class ScheduleDisplayItem extends Component {
  handleDeleteMenuItemClick = (scheduleUUID) => {
    this.props.handleDeleteAction(scheduleUUID);
  }
  render() {
    const {
      name,
      idx
    } = this.props;
    const schedule = _.cloneDeep(this.props.schedule);

    const getReadableTime = (msecs) => {
      const time = moment.duration(msecs);
      const timeArray = [
        [time.seconds(), 'sec'],
        [time.minutes(), 'min'],
        [time.hours(), 'hour'],
        [time.days(), 'day'],
        [time.months(), 'month'],
        [time.years(),  'year'],
      ];

      const reducedTimeArray = timeArray.filter(item => item[0] !== 0).slice(0, 2);
      return isDefinedNotNull(reducedTimeArray[1])
        ? `Every ${reducedTimeArray[1][0]}
           ${pluralize(reducedTimeArray[1][1], reducedTimeArray[1][0])}
           ${reducedTimeArray[0][0]}
           ${pluralize(reducedTimeArray[0][1], reducedTimeArray[0][0])}`
        : `Every ${reducedTimeArray[0][0]}
           ${pluralize(reducedTimeArray[0][1], reducedTimeArray[0][0])}`;
    };

    if (schedule.frequency) {
      schedule.frequency = getReadableTime(schedule.frequency);
    }
    if (schedule.cronExpression) {
      schedule.cronExpression = cronstrue.toString(schedule.cronExpression);
    }

    const taskType = (() => {
      switch (schedule.taskType) {
        case "BackupUniverse":
          return "Table Backup";
        case "MultiTableBackup":
          return "Full Universe Backup";
        default:
          break;
      }
    })();

    const tableDetails = (() => {
      switch (schedule.taskType) {
        case "BackupUniverse":
          return `${schedule.taskParams.keyspace}.${schedule.taskParams.tableName}`;
        case "MultiTableBackup":
          return "All";
        default:
          break;
      }
    })();

    return (
      <Col xs={12} sm={6} md={6} lg={4}>
        <div className="schedule-display-item-container">
          <div className="status-icon">
            <DropdownButton
              bsStyle={"default"}
              title={<span className="fa fa-ellipsis-v"/>}
              key={name+idx}
              noCaret
              pullRight
              id={`dropdown-basic-${name}-${idx}`}
            >
              <MenuItem
                eventKey="1"
                className="menu-item-delete"
                onClick={this.handleDeleteMenuItemClick.bind(this, schedule.scheduleUUID)}
              >
                <span className="fa fa-trash"/>Delete schedule
              </MenuItem>
            </DropdownButton>
          </div>
          <div className="display-name">
            {name}
          </div>
          <div className="provider-name">
            {taskType}
          </div>
          <div className="description-item-list">
            <DescriptionItem title={"Schedule"}>
              <span>{schedule.frequency || schedule.cronExpression}</span>
            </DescriptionItem>
            <DescriptionItem title={"Server-Side Encryption"}>
              <span>{schedule.taskParams.sse ? "On" : "Off"}</span>
            </DescriptionItem>

            <DescriptionItem title={"Table"}>
              <span>{tableDetails}</span>
            </DescriptionItem>
          </div>
        </div>
      </Col>
    );
  }
}

class Schedules extends Component {

  constructor(props) {
    super(props);
    this.state = {
      scheduleUUID: null
    };
  }

  handleDeleteAction = (scheduleUUID) => {
    const { showAddCertificateModal } = this.props;
    showAddCertificateModal();
    this.setState({scheduleUUID});
  }

  handleDeleteSchedule = () => {
    const { deleteSchedule } = this.props;
    const scheduleUUID = this.state.scheduleUUID;
    deleteSchedule(scheduleUUID);
    this.setState({scheduleUUID: null});
  }


  componentDidMount() {
    this.props.getSchedules();
    this.props.fetchUniverseList();
  }

  render() {
    const {
      customer: { schedules, deleteSchedule },
      universe: { universeList },
      closeModal,
      modal,
      modal: {
        visibleModal,
        showModal
      }
    } = this.props;

    const findUniverseName = (uuid) => {
      if (getPromiseState(universeList).isSuccess()) {
        return universeList.data.find(universe => universe.universeUUID === uuid).name;
      } else {
        return null;
      }
    };

    let schedulesList = <span>There is no data to display</span>;
    if (getPromiseState(schedules).isSuccess() && isNonEmptyArray(schedules.data) && getPromiseState(universeList).isSuccess()) {
      schedulesList = schedules.data.map((scheduleItem, idx) => {
        return (<ScheduleDisplayItem
          key={scheduleItem.name + scheduleItem.taskType + idx}
          idx={idx}
          modal={modal}
          handleDeleteAction={this.handleDeleteAction}
          name={findUniverseName(scheduleItem.taskParams.universeUUID)}
          schedule={scheduleItem}
        />);
      });
    }

    return (
      <div id="page-wrapper">
        <YBPanelItem
          className={"schedules-panel"}
          noBackground={true}
          header={
            <h2 className="content-title">Schedules</h2>
          }
          body={
            <Fragment>

              <YBModalForm
                title={"Confirm Delete Schedule"}
                visible={showModal && visibleModal === "deleteScheduleModal"}
                className={getPromiseState(deleteSchedule).isError() ? "modal-shake " : ""}
                onHide={closeModal}
                showCancelButton={true}
                cancelLabel={"Cancel"}
                onFormSubmit={this.handleDeleteSchedule}
              >
                Are you sure you want to delete reccuring event with ScheduleUUID {this.state.scheduleUUID}

                { getPromiseState(deleteSchedule).isError() &&
                  isNonEmptyObject(deleteSchedule.error) &&
                  <Alert bsStyle={'danger'} variant={'danger'}>
                    Schedule deleting went wrong:<br/>
                    {JSON.stringify(deleteSchedule.error)}
                  </Alert>
                }
              </YBModalForm>
              <Row>
                {schedulesList}
              </Row>
            </Fragment>
          }
        />
      </div>
    );
  }
}

export default Schedules;
