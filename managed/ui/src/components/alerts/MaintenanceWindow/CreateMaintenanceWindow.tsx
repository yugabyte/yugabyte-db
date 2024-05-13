import { Field, Form, Formik } from 'formik';
import React, { ChangeEvent, FC } from 'react';
import { Col, Row } from 'react-bootstrap';
import {
  YBButton,
  YBControlledTextInput,
  YBMultiSelectWithLabel,
  YBTextArea
} from '../../common/forms/fields';
import moment from 'moment';
import '../../metrics/CustomDatePicker/CustomDatePicker.scss';
import './CreateMaintenanceWindow.scss';
import CautionIcon from './CautionIcon';
import * as Yup from 'yup';
import { useMutation } from 'react-query';
import {
  convertUTCStringToDate,
  createMaintenanceWindow,
  MaintenanceWindowSchema,
  updateMaintenanceWindow
} from '.';
import { toast } from 'react-toastify';
import { createErrorMessage } from '../../../utils/ObjectUtils';
import { convertToISODateString, YBTimeFormats } from '../../../redesign/helpers/DateUtils';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const reactWidgets = require('react-widgets');
// eslint-disable-next-line @typescript-eslint/no-var-requires
const momentLocalizer = require('react-widgets-moment');
require('react-widgets/dist/css/react-widgets.css');

const { DateTimePicker } = reactWidgets;
momentLocalizer(moment);

interface CreateMaintenanceWindowProps {
  universeList: Array<{ universeUUID: string; name: string }>;
  showListView: () => void;
  selectedWindow: MaintenanceWindowSchema | null;
}

// eslint-disable-next-line @typescript-eslint/no-unused-vars
enum TARGET_OPTIONS {
  ALL = 'all',
  SELECTED = 'selected'
}

const targetOptions = [
  { label: 'All Universes', value: TARGET_OPTIONS.ALL },
  { label: 'Selected Universes', value: TARGET_OPTIONS.SELECTED }
];

const supressUniverseOptions = [
  { label: 'All Universes', value: true },
  { label: 'Selected Universes', value: false }
];

const initialValues = {
  target: TARGET_OPTIONS.ALL,
  selectedUniverse: [],
  suppressHealthCheckNotificationsConfig: {
    suppressAllUniverses: true,
    universeUUIDSet: [] as any[]
  }
};

const DATE_FORMAT = YBTimeFormats.YB_DATE_ONLY_TIMESTAMP;

const validationSchema = Yup.object().shape({
  name: Yup.string().required('Enter name'),
  description: Yup.string().required('Enter description'),
  startTime: Yup.string().required('Enter start time'),
  endTime: Yup.string().required('Enter end time'),
  target: Yup.string().required('select a target'),
  selectedUniverse: Yup.array().when('target', {
    is: TARGET_OPTIONS.SELECTED,
    then: Yup.array().min(1, 'atleast one universe has to be selected')
  }),
  suppressHealthCheckNotificationsConfig: Yup.object().shape({
    universeUUIDSet: Yup.array().when('suppressAllUniverses', {
      is: (e) => !e,
      then: Yup.array().min(1, 'atleast one universe has to be selected')
    })
  })
});

/**
 * Create Maintenance Window Component
 * @param universeList List of universes
 * @param showListView switch back to list view
 * @param selectedWindow current selected window
 * @returns
 */
export const CreateMaintenanceWindow: FC<CreateMaintenanceWindowProps> = ({
  universeList,
  showListView,
  selectedWindow
}) => {
  const createWindow = useMutation(
    (values: MaintenanceWindowSchema) => {
      return createMaintenanceWindow({
        ...values,
        alertConfigurationFilter: {
          targetType: 'UNIVERSE',
          target: {
            all: values['target'] === TARGET_OPTIONS.ALL,
            uuids: values['selectedUniverse'].map((universe: any) => universe.value)
          }
        },
        suppressHealthCheckNotificationsConfig: {
          suppressAllUniverses:
            values['suppressHealthCheckNotificationsConfig'].suppressAllUniverses,
          universeUUIDSet: values['suppressHealthCheckNotificationsConfig'].universeUUIDSet.map(
            (universe: any) => universe.value
          )
        }
      });
    },
    {
      onSuccess: () => {
        toast.success('Maintenance Window created sucessfully!');
        showListView();
      },
      onError: (err) => {
        const errMsg = createErrorMessage(err);
        toast.error(errMsg);
      }
    }
  );

  const updateWindow = useMutation(
    (values: MaintenanceWindowSchema) => {
      return updateMaintenanceWindow({
        ...values,
        alertConfigurationFilter: {
          targetType: 'UNIVERSE',
          target: {
            all: values['target'] === TARGET_OPTIONS.ALL,
            uuids: values['selectedUniverse'].map((universe: any) => universe.value)
          }
        },
        suppressHealthCheckNotificationsConfig: {
          suppressAllUniverses:
            values['suppressHealthCheckNotificationsConfig'].suppressAllUniverses,
          universeUUIDSet: values['suppressHealthCheckNotificationsConfig'].universeUUIDSet.map(
            (universe: any) => universe.value
          )
        }
      });
    },
    {
      onSuccess: () => {
        toast.success('Maintenance Window updated sucessfully!');
        showListView();
      }
    }
  );

  const universes = universeList.map((universe) => {
    return { label: universe.name, value: universe.universeUUID };
  });

  const findUniverseNamesByUUIDs = (uuids: string[]) => {
    return universeList
      .filter((universe) => uuids.includes(universe.universeUUID))
      .map((universe) => {
        return { label: universe.name, value: universe.universeUUID };
      });
  };

  /**
   * prepares initial values for formik from maintenanceWindowsSchema
   * @returns map of values or null
   */
  const getInitialValues = () => {
    if (!selectedWindow) return initialValues;
    const selectedUniverse = findUniverseNamesByUUIDs(
      selectedWindow?.alertConfigurationFilter.target.uuids
    );
    const universeUUIDSet = findUniverseNamesByUUIDs(
      selectedWindow?.suppressHealthCheckNotificationsConfig.universeUUIDSet
    );

    return {
      ...selectedWindow,
      target: selectedWindow?.alertConfigurationFilter.target.all
        ? TARGET_OPTIONS.ALL
        : TARGET_OPTIONS.SELECTED,
      selectedUniverse,
      suppressHealthCheckNotificationsConfig: {
        suppressAllUniverses:
          selectedWindow.suppressHealthCheckNotificationsConfig.suppressAllUniverses,
        universeUUIDSet
      }
    };
  };

  return (
    <Formik
      initialValues={getInitialValues()}
      onSubmit={(values) =>
        selectedWindow === null
          ? createWindow.mutateAsync((values as unknown) as MaintenanceWindowSchema)
          : updateWindow.mutateAsync((values as unknown) as MaintenanceWindowSchema)
      }
      validationSchema={validationSchema}
      validateOnBlur={false}
    >
      {({ handleSubmit, setFieldValue, values, errors }) => (
        <Form className="create-maintenance-window">
          <Row>
            <Col lg={6}>
              <Field
                name="name"
                label="Name"
                component={YBControlledTextInput}
                onValueChanged={(event: ChangeEvent<HTMLInputElement>) =>
                  setFieldValue('name' as never, event.target.value, false)
                }
                placeHolder="Enter window name"
                val={values['name']}
              />
              <span className="field-error">{errors['name']}</span>
            </Col>
          </Row>
          <Row>
            <Col lg={6}>
              <Field
                name="description"
                label="Description"
                component={YBTextArea}
                placeHolder="Enter description"
                input={{
                  value: values['description'],
                  onChange: (value: string) => setFieldValue('description' as never, value, false)
                }}
              />
              <span className="field-error">{errors['description']}</span>
            </Col>
          </Row>
          <Row>
            <Col lg={2} className="date-picker-start">
              <div className="time-label">Start Time</div>
              <DateTimePicker
                placeholder="Pick a time"
                step={10}
                formats={DATE_FORMAT}
                min={new Date()}
                onChange={(time: Date) =>
                  setFieldValue('startTime' as never, convertToISODateString(time), false)
                }
                defaultValue={
                  values['startTime'] ? convertUTCStringToDate(values['startTime']) : null
                }
              />
              <span className="field-error">{errors['startTime']}</span>
            </Col>
            <Col lg={2}>
              <div className="time-label">End Time</div>
              <DateTimePicker
                placeholder="Pick a time"
                formats={DATE_FORMAT}
                step={10}
                min={moment(new Date()).add(1, 'm').toDate()}
                onChange={(time: Date) =>
                  setFieldValue('endTime' as never, convertToISODateString(time), false)
                }
                defaultValue={values['endTime'] ? convertUTCStringToDate(values['endTime']) : null}
              />
              <span className="field-error">{errors['endTime']}</span>
            </Col>
          </Row>
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Target</div>
              {targetOptions.map((target) => (
                <label className="btn-group btn-group-radio" key={target.value}>
                  <Field
                    name="alert"
                    component="input"
                    defaultChecked={values['target'] === target.value}
                    onChange={(e: React.ChangeEvent<HTMLInputElement>) =>
                      setFieldValue('target' as never, e.target.value, false)
                    }
                    type="radio"
                    value={target.value}
                  />
                  {target.label}
                </label>
              ))}
              <Field
                component={YBMultiSelectWithLabel}
                options={universes}
                hideSelectedOptions={false}
                isMulti={true}
                input={{
                  defaultValue: values['selectedUniverse'],
                  onChange: (values: string[]) => {
                    setFieldValue('selectedUniverse' as never, values ?? [], false);
                  }
                }}
                validate={false}
                className={values['target'] !== 'selected' ? 'hide-field' : ''}
              />
              <span className="field-error">{errors['selectedUniverse']}</span>
            </Col>
          </Row>
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Suppress Health Check Notifications</div>
              {supressUniverseOptions.map((target) => (
                <label className="btn-group btn-group-radio" key={target.value + ''}>
                  <Field
                    name="suppressHealthCheckNotificationsConfig.suppressAllUniverses"
                    component="input"
                    defaultChecked={
                      values['suppressHealthCheckNotificationsConfig'].suppressAllUniverses ===
                      target.value
                    }
                    onChange={(e: React.ChangeEvent<HTMLInputElement>) =>
                      setFieldValue(
                        'suppressHealthCheckNotificationsConfig.suppressAllUniverses' as never,
                        target.value,
                        false
                      )
                    }
                    type="radio"
                    value={target.value}
                  />
                  {target.label}
                </label>
              ))}
              <Field
                component={YBMultiSelectWithLabel}
                options={universes}
                hideSelectedOptions={false}
                isMulti={true}
                input={{
                  defaultValue: values['suppressHealthCheckNotificationsConfig'].universeUUIDSet,
                  onChange: (values: string[]) => {
                    setFieldValue(
                      'suppressHealthCheckNotificationsConfig.universeUUIDSet' as never,
                      values ?? [],
                      false
                    );
                  }
                }}
                validate={false}
                className={
                  values['suppressHealthCheckNotificationsConfig'].suppressAllUniverses
                    ? 'hide-field'
                    : ''
                }
              />
              <span className="field-error">
                {errors['suppressHealthCheckNotificationsConfig']?.universeUUIDSet}
              </span>
            </Col>
          </Row>
          <Row className="action-btns-margin">
            <Col lg={4}>
              <div>Affected Alerts</div>
              <div className="alert-notice">
                <span className="icon">
                  <CautionIcon />
                </span>
                All alerts will be snoozed during the defined time range.
              </div>
            </Col>
          </Row>
          <Row className="maintenance-action-button-container">
            <Col lg={6} lgOffset={6}>
              <YBButton
                btnText="Cancel"
                btnClass="btn"
                onClick={() => {
                  showListView();
                }}
              />
              <YBButton
                btnText="Save"
                onClick={handleSubmit}
                btnType="submit"
                btnClass="btn btn-orange"
              />
            </Col>
          </Row>
        </Form>
      )}
    </Formik>
  );
};
