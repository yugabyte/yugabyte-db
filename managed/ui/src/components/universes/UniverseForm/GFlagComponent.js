import React, { useState, useEffect } from 'react';
import {
  Alert,
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
//Icons
import Edit from '../images/edit_pen.svg';
import Close from '../images/close.svg';
import Plus from '../images/plus.svg';
import MoreIcon from '../images/ellipsis.svg';
// Styles
import './UniverseForm.scss';

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
        Add as JSON
      </>
    ),
    className: 'btn btn-default'
  }
];
//server list - TO DRY THE CODE
const SERVER_LIST = [
  {
    serverName: TSERVER,
    label: 'Add to T-Server'
  },
  {
    serverName: MASTER,
    label: 'Add to Master'
  }
];

export default function GFlagComponent(props) {
  const { fields, dbVersion, isReadOnly, editMode } = props;
  const [selectedProps, setSelectedProps] = useState(null);
  const [toggleModal, setToggleModal] = useState(false);
  const [validationError, setValidationError] = useState([]);
  const [formError, setFormError] = useState(null);
  const [versionError, setVersionError] = useState(null);
  const whenMounted = useWhenMounted();

  //handlers
  const handleSelectedOption = (serverProps) => {
    setSelectedProps(serverProps);
    setToggleModal(true);
    setFormError(null);
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
      setVersionError(e?.error);
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
    switch (values.option) {
      case FREE_TEXT: {
        try {
          const formValues = JSON.parse(values?.flagvalue);
          const newFlagArr = [];
          if (Object.keys(formValues).length > 0) {
            Object.entries(formValues).forEach(([key, val]) => {
              const obj = { Name: key, [values?.server]: val };
              checkExistsAndPush(obj);
              newFlagArr.push(obj);
            });
            callValidation(newFlagArr);
            setToggleModal(false);
          }
        } catch (e) {
          setFormError('Fix all the errors shown below to proceed');
          setTimeout(() => {
            setFormError(null);
            actions.setSubmitting(false);
          }, 5000);
        }
        break;
      }

      case ADD_GFLAG: {
        const obj = {
          Name: values?.flagname,
          [values?.server]: values?.flagvalue,
          tags: values?.tags
        };
        checkExistsAndPush(obj);
        callValidation([obj]);
        setToggleModal(false);
        break;
      }

      default:
        break;
    }
  };

  const onVersionChange = () => {
    setVersionError(null);
    whenMounted(async () => {
      if (fields.length > 0) {
        try {
          const payload = { gflags: fields.getAll() };
          const validationResponse = await validateGFlags(dbVersion, payload);
          setValidationError(validationResponse?.data);
        } catch (e) {
          setVersionError(e?.error);
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
      <Button bsClass="flag-icon-button ml-10 mt-2">
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
        <div className="cell-font">{cell}</div>
        <div className="icons">
          <div className="flag-icons">
            <OverlayTrigger
              placement="top"
              overlay={
                <Tooltip className="high-index" id="remove-flag">
                  Remove Flag
                </Tooltip>
              }
            >
              <Button bsClass="flag-icon-button mb-2" onClick={() => fields.remove(index)}>
                <img alt="Remove" src={Close} width="22" />
              </Button>
            </OverlayTrigger>
            &nbsp;
          </div>
          {flagDoesNotExists &&
            errorPopover('Incorrect flag name', 'Flag name is incorrect. Remove Flag')}
        </div>
      </div>
    );
  };

  const handleRemoveFlag = (rowObj, i, serverType, removeFlag) => {
    if (removeFlag) {
      fields.remove(i);
      const newObj = _.pick(rowObj, ['Name', serverType === MASTER ? TSERVER : MASTER]);
      fields.insert(i, newObj);
    } else fields.remove(i);
  };

  const valueFormatter = (cell, row, index, server) => {
    const valueExists = cell !== undefined;
    const eInfo = validationError?.find((e) => e.Name === row?.Name); //error info
    const isError = eInfo?.[server]?.error;
    const isFlagExist = eInfo?.[server]?.exist === true;
    const notExists = eInfo?.[server]?.exist === false;

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

      const checkFlagExistsOnOtherServer = (serverType) => {
        return (
          eInfo &&
          (eInfo[MASTER]?.exist === true || eInfo[TSERVER]?.exist === true) &&
          // eslint-disable-next-line no-prototype-builtins
          row?.hasOwnProperty(serverType === MASTER ? TSERVER : MASTER)
        );
      };

      return (
        <div className={clsx('table-val-column', isError && 'error-val-column')}>
          <div className="cell-font">{`${cell}`}</div>
          <div className="icons">
            <div className="more-icon">
              <Button
                bsClass="flag-icon-button mb-2"
                onClick={() => handleSelectedOption(modalProps)}
              >
                <img alt="More" src={MoreIcon} width="20" />
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
                    bsClass="flag-icon-button mr-10 mb-2"
                    onClick={() => handleSelectedOption(modalProps)}
                  >
                    <img alt="Edit" src={Edit} width="20" />
                  </Button>
                </OverlayTrigger>
              )}
              &nbsp;
              <OverlayTrigger
                placement="top"
                overlay={
                  <Tooltip className="high-index" id="remove-flag">
                    {checkFlagExistsOnOtherServer(server) ? 'Remove Value' : 'Remove Flag'}
                  </Tooltip>
                }
              >
                <Button
                  bsClass="flag-icon-button mb-2"
                  onClick={() =>
                    handleRemoveFlag(row, index, server, checkFlagExistsOnOtherServer(server))
                  }
                >
                  <img alt="Remove" src={Close} width="22" />
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
          </div>
        </div>
      );
    } else {
      modalProps = {
        ...modalProps,
        flagname: row?.Name
      };
      return (
        <div className={clsx('table-val-column', 'empty-cell', notExists && 'no-border')}>
          {isFlagExist && (
            <Button
              bsClass="flag-icon-button display-inline-flex mb-2"
              onClick={() => handleSelectedOption(modalProps)}
            >
              <img alt="Add" src={Plus} width="20" />
              <span className="add-label">Add value</span>
            </Button>
          )}
          {notExists && <span className="cell-font muted-text">n/a</span>}
        </div>
      );
    }
  };

  const renderTable = () => {
    return (
      <div className={isReadOnly ? 'gflag-read-table' : 'gflag-edit-table'}>
        <BootstrapTable
          data={fields.getAll()}
          height={editMode ? '420px' : 'auto'}
          maxHeight="420px"
          tableStyle={{ overflow: 'scroll' }}
        >
          <TableHeaderColumn width="40%" dataField="Name" dataFormat={nameFormatter} isKey>
            <span className="header-title">FLAG NAME</span>
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="TSERVER"
            width="30%"
            dataFormat={(cell, row, e, index) => valueFormatter(cell, row, index, TSERVER)}
          >
            <span className="header-title">T-SERVER VALUE</span>
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="MASTER"
            width="30%"
            dataFormat={(cell, row, e, index) => valueFormatter(cell, row, index, MASTER)}
          >
            <span className="header-title">MASTER VALUE</span>
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
        return (
          <AddGFlag
            formProps={formProps}
            gFlagProps={{ ...selectedProps, dbVersion, existingFlags: fields.getAll() }}
          />
        );

      default:
        return null;
    }
  };

  const renderModal = () => {
    const gflagSchema = Yup.object().shape({
      flagvalue: Yup.mixed().required('This field is required')
    });
    const modalTitle = selectedProps?.mode === CREATE ? selectedProps?.label : 'Edit Flag Value';
    const modalLabel = selectedProps?.mode === CREATE ? 'Add Flag' : 'Confirm';
    return (
      <YBModalForm
        title={modalTitle}
        visible={toggleModal}
        submitLabel={modalLabel}
        formName="ADDGFlagForm"
        cancelLabel="Cancel"
        error={formError}
        validationSchema={gflagSchema}
        showCancelButton={true}
        onHide={() => setToggleModal(false)}
        onFormSubmit={handleFormSubmit}
        render={(properties) => renderOption(properties)}
        dialogClassName={toggleModal ? 'gflag-modal modal-fade in' : 'modal-fade'}
        headerClassName="add-flag-header"
        showBackButton={true}
      />
    );
  };
  const renderBanner = () => {
    return (
      <div className="gflag-empty-banner">
        <span className="empty-text">There are no flags assigned to this universe </span>
      </div>
    );
  };

  return (
    <FlexContainer direction="column">
      {versionError && (
        <Alert bsStyle="danger" variant="danger">
          {versionError} (Selected DB Version : <b>{dbVersion}</b>)
        </Alert>
      )}
      <FlexShrink>
        {!isReadOnly &&
          OPTIONS.map((option) => {
            const { optionName, ...rest } = option;
            return (
              <DropdownButton {...rest} bsSize="small" id={optionName} key={optionName}>
                {SERVER_LIST.map((server) => {
                  const { serverName, label } = server;
                  const serverProps = {
                    option: optionName,
                    server: serverName,
                    mode: CREATE,
                    label
                  };
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
      {fields.length <= 0 && editMode && renderBanner()}
      {fields.length > 0 && renderTable()}
      {toggleModal && renderModal()}
    </FlexContainer>
  );
}
