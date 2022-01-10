import React, { useState, useEffect } from 'react';
import {
  DropdownButton,
  MenuItem,
  Button,
  Tooltip,
  OverlayTrigger,
  Popover
} from 'react-bootstrap';
import _ from 'lodash';
import * as Yup from 'yup';
import clsx from 'clsx';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { YBModalForm } from '../../common/forms';
import AddGFlag from './AddGFlag';
import EditorGFlag from './EditorGFlag';
import { validateGFlags } from '../../../actions/universe';
import { useWhenMounted } from '../../../redesign/helpers/hooks';

//server
const MASTER = 'MASTER';
const TSERVER = 'TSERVER';
//options
const FREE_TEXT = 'FREE_TEXT';
const ADD_GFLAG = 'ADD_GFLAG';
//modes
const CREATE = 'CREATE';
const EDIT = 'EDIT';
//options Array -- TO DRY THE CODE
const OPTIONS = [
  {
    optionName: ADD_GFLAG,
    title: (
      <>
        <span className="fa fa-plus" />
        Add Flags
      </>
    ),
    className: 'btn btn-orange mr-10',
    bsStyle: 'danger'
  },
  {
    optionName: FREE_TEXT,
    title: (
      <>
        <span className="fa fa-edit" />
        Add as Free text
      </>
    ),
    className: 'btn btn-default'
  }
];
//server list - TO DRY THE CODE
const SERVER_LIST = [
  {
    serverName: MASTER,
    label: 'Add to Master'
  },
  {
    serverName: TSERVER,
    label: 'Add to T-Server'
  }
];

export default function GFlagComponent(props) {
  const { fields, dbVersion, isReadOnly } = props;
  const [selectedProps, setSelectedProps] = useState(null);
  const [toggleModal, setToggleModal] = useState(false);
  const [validationError, setValidationError] = useState([]);
  const whenMounted = useWhenMounted();

  //handlers
  const handleSelectedOption = (serverProps) => {
    setSelectedProps(serverProps);
    setToggleModal(true);
  };

  const callValidation = async (flagArr) => {
    try {
      const currentArr = fields.getAll() || [];
      const duplicateArr = _.remove(currentArr, (e) => flagArr.some((f) => f.Name === e.Name));
      const transformedArr = flagArr.map((e) => {
        const dupEl = duplicateArr.find((f) => f.Name === e.Name) || {};
        return { ...dupEl, ...e };
      });
      const payload = { gflags: [...currentArr, ...transformedArr] };
      const validationResponse = await validateGFlags(dbVersion, payload);
      setValidationError(validationResponse?.data);
    } catch (e) {
      console.error(e);
    }
  };

  const checkExistsAndPush = (flagObj) => {
    try {
      const allFields = fields.getAll() || [];
      const fieldIndex = allFields?.findIndex((f) => f?.Name === flagObj?.Name);
      if (fieldIndex > -1) {
        fields.remove(fieldIndex);
        fields.insert(fieldIndex, { ...allFields[fieldIndex], ...flagObj });
      } else fields.push(flagObj);
    } catch (e) {
      console.error(e);
    }
  };

  const handleFormSubmit = (values, actions) => {
    setToggleModal(false);
    switch (values.option) {
      case FREE_TEXT: {
        const formValues = JSON.parse(values?.flagvalue);
        const newFlagArr = [];
        if (Object.keys(formValues).length > 0) {
          Object.entries(formValues).forEach(([key, val]) => {
            const obj = { Name: key, [values?.server]: val };
            checkExistsAndPush(obj);
            newFlagArr.push(obj);
          });
          callValidation(newFlagArr);
        }
        break;
      }

      case ADD_GFLAG: {
        const obj = { Name: values?.flagname, [values?.server]: values?.flagvalue };
        checkExistsAndPush(obj);
        callValidation([obj]);
        break;
      }

      default:
        break;
    }
  };

  const onVersionChange = () => {
    whenMounted(async () => {
      if (fields.length > 0) {
        try {
          const payload = { gflags: fields.getAll() };
          const validationResponse = await validateGFlags(dbVersion, payload);
          setValidationError(validationResponse?.data);
        } catch (e) {
          console.error(e);
        }
      }
    });
  };

  useEffect(onVersionChange, [dbVersion]);

  const errorPopover = (title, msg) => (
    <OverlayTrigger
      trigger={['hover', 'click']}
      placement="top"
      overlay={
        <Popover
          id="popover-trigger-hover-focus"
          className="popover-container"
          title={<span>{title}</span>}
        >
          {msg}
        </Popover>
      }
    >
      <Button bsClass="flag-icon-button">
        <i className="fa fa-exclamation-triangle error-icon" />
      </Button>
    </OverlayTrigger>
  );

  const nameFormatter = (cell, row, e, index) => {
    const eInfo = validationError?.find((e) => e.Name === cell); //error info
    const flagDoesNotExists =
      eInfo && eInfo[MASTER]?.exist === false && eInfo[TSERVER]?.exist === false;

    if (isReadOnly) return <span className="cell-font">{cell}</span>;
    return (
      <div className={clsx('name-container', flagDoesNotExists && 'error-val-column')}>
        <span className="cell-font">{cell}</span>
        <FlexContainer className="icons" direction="row">
          <div className="flag-icons">
            <OverlayTrigger
              placement="top"
              overlay={
                <Tooltip className="high-index" id="remove-flag">
                  Remove Flag
                </Tooltip>
              }
            >
              <Button bsClass="flag-icon-button" onClick={() => fields.remove(index)}>
                <i className="fa fa-close" />
              </Button>
            </OverlayTrigger>
            &nbsp;
          </div>
          {flagDoesNotExists &&
            errorPopover('Incorrect flag name', 'Flag name is incorrect. Remove Flag')}
        </FlexContainer>
      </div>
    );
  };

  const valueFormatter = (cell, row, index, server) => {
    const valueExists = cell !== undefined;
    const eInfo = validationError?.find((e) => e.Name === row?.Name); //error info
    const isError = eInfo && eInfo[server]?.error;
    const isFlagExist = eInfo && eInfo[server]?.exist === true;
    const notExists = eInfo && eInfo[server]?.exist === false;
    let modalProps = {
      server,
      option: ADD_GFLAG,
      mode: EDIT
    };

    if (isReadOnly) return <span className="cell-font">{valueExists ? `${cell}` : ''}</span>;
    if (valueExists) {
      modalProps = {
        ...modalProps,
        flagname: row?.Name,
        flagvalue: row[server]
      };
      if (isError) modalProps['errorMsg'] = eInfo[server]?.error;

      return (
        <div className={clsx('table-val-column', isError && 'error-val-column')}>
          <span className="cell-font">{`${cell}`}</span>
          <FlexContainer className="icons" direction="row">
            <div className="more-icon">
              <Button bsClass="flag-icon-button" onClick={() => handleSelectedOption(modalProps)}>
                <i className="fa fa-ellipsis-h"></i>
              </Button>
            </div>
            <div className="flag-icons">
              {isFlagExist && (
                <OverlayTrigger
                  placement="top"
                  overlay={
                    <Tooltip className="high-index" id="edit-flag">
                      Edit Flag
                    </Tooltip>
                  }
                >
                  <Button
                    bsClass="flag-icon-button"
                    onClick={() => handleSelectedOption(modalProps)}
                  >
                    <i className="fa fa-pencil"></i>
                  </Button>
                </OverlayTrigger>
              )}
              &nbsp;
              <OverlayTrigger
                placement="top"
                overlay={
                  <Tooltip className="high-index" id="remove-flag">
                    Remove Flag
                  </Tooltip>
                }
              >
                <Button bsClass="flag-icon-button" onClick={() => fields.remove(index)}>
                  <i className="fa fa-close" />
                </Button>
              </OverlayTrigger>
            </div>
            &nbsp;
            {(isError || notExists) &&
              errorPopover(
                'Incorrect flag value',
                isFlagExist
                  ? eInfo[server]?.error
                  : `This Flag does not belong to ${server}. Remove Flag`
              )}
          </FlexContainer>
        </div>
      );
    } else {
      modalProps = {
        ...modalProps,
        flagname: row?.Name
      };
      return (
        <div className="table-val-column">
          {isFlagExist && (
            <Button bsClass="flag-icon-button" onClick={() => handleSelectedOption(modalProps)}>
              <i className="fa fa-plus"></i>
              <span className="add-label cell-font">Add value</span>
            </Button>
          )}
          {notExists && <span className="cell-font muted-text">n/a</span>}
        </div>
      );
    }
  };

  const renderTable = () => {
    return (
      <div className="gflag-table">
        <BootstrapTable data={fields.getAll()}>
          <TableHeaderColumn width="40%" dataField="Name" dataFormat={nameFormatter} isKey>
            FLAG NAME
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="MASTER"
            width="30%"
            dataFormat={(cell, row, e, index) => valueFormatter(cell, row, index, MASTER)}
          >
            MASTER VALUE
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="TSERVER"
            width="30%"
            dataFormat={(cell, row, e, index) => valueFormatter(cell, row, index, TSERVER)}
          >
            T-SERVER VALUE
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    );
  };

  const renderOption = (formProps) => {
    switch (selectedProps?.option) {
      case FREE_TEXT: {
        return <EditorGFlag formProps={formProps} gFlagProps={{ ...selectedProps, dbVersion }} />;
      }

      case ADD_GFLAG:
        return <AddGFlag formProps={formProps} gFlagProps={{ ...selectedProps, dbVersion }} />;

      default:
        return null;
    }
  };

  const renderModal = () => {
    const gflagSchema = Yup.object().shape({
      flagvalue: Yup.mixed().required('This field is required')
    });
    return (
      <YBModalForm
        title={SERVER_LIST.find((e) => e.serverName === selectedProps?.server).label}
        visible={toggleModal}
        size="large"
        submitLabel="Add Flag"
        formName="ADDGFlagForm"
        cancelLabel="Cancel"
        validationSchema={gflagSchema}
        showCancelButton={true}
        onHide={() => setToggleModal(false)}
        onFormSubmit={handleFormSubmit}
        render={(properties) => renderOption(properties)}
      />
    );
  };

  return (
    <FlexContainer direction="column">
      <FlexShrink>
        {!isReadOnly &&
          OPTIONS.map((option) => {
            const { optionName, ...rest } = option;
            return (
              <DropdownButton {...rest} id={optionName} key={optionName}>
                {SERVER_LIST.map((server) => {
                  const { serverName, label } = server;
                  const serverProps = { option: optionName, server: serverName, mode: CREATE };
                  return (
                    <MenuItem
                      key={optionName + '-' + serverName}
                      onClick={() => handleSelectedOption(serverProps)}
                    >
                      {label}
                    </MenuItem>
                  );
                })}
              </DropdownButton>
            );
          })}
      </FlexShrink>
      {fields.length > 0 && renderTable()}
      {toggleModal && renderModal()}
    </FlexContainer>
  );
}
