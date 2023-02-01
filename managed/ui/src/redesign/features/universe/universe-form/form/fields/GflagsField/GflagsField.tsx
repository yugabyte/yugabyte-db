import React, { useState, ReactElement } from 'react';
import _ from 'lodash';
import * as Yup from 'yup';
import clsx from 'clsx';
import { Box } from '@material-ui/core';
import { Control, useFieldArray } from 'react-hook-form';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import {
  Alert,
  DropdownButton,
  MenuItem,
  Button,
  Tooltip,
  OverlayTrigger,
  Popover
} from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBModalForm } from '../../../../../../../components/common/forms';
import AddGFlag from '../../../../../../../components/universes/UniverseForm/AddGFlag';
import EditorGFlag from '../../../../../../../components/universes/UniverseForm/EditorGFlag';
import { useWhenMounted } from '../../../../../../helpers/hooks';
import { validateGFlags } from '../../../../../../../actions/universe';
import { UniverseFormData, Gflag } from '../../../utils/dto';
//Icons
import Edit from '../../../../../../assets/edit_pen.svg';
import Close from '../../../../../../assets/close.svg';
import Plus from '../../../../../../assets/plus.svg';
import MoreIcon from '../../../../../../assets/ellipsis.svg';

/* TODO : 
1. Rewrite this file with proper types
2. Integrate with react-query
3. Rewrite AddGflag and EditorGflag with typescript and react-query
4. Rewrite with using material ui components
*/

interface GflagsFieldProps {
  dbVersion: string;
  fieldPath: string;
  control: Control<Partial<UniverseFormData>>;
  editMode: boolean;
  isReadOnly: boolean;
}

interface SelectedOption {
  option: string;
  server: string;
  mode?: string;
  label?: string;
  flagname?: string;
  flagvalue?: any;
}

interface GflagValidationObject {
  exist?: boolean;
  error?: string | null;
}

interface GflagValidationResponse {
  Name: string;
  MASTER?: GflagValidationObject;
  TSERVER?: GflagValidationObject;
}

export type SERVER = 'MASTER' | 'TSERVER';

//server
const MASTER = 'MASTER';
const TSERVER = 'TSERVER';
//options
const FREE_TEXT = 'FREE_TEXT';
const ADD_GFLAG = 'ADD_GFLAG';
//modes
const CREATE = 'CREATE';
const EDIT = 'EDIT';

export type GFlagWithId = Gflag & { id: string };

export const GFlagsField = ({
  dbVersion,
  fieldPath,
  control,
  editMode = false,
  isReadOnly = false
}: GflagsFieldProps): ReactElement => {
  const { fields, append, insert, remove } = useFieldArray({
    name: fieldPath as any,
    control
  });
  const [selectedProps, setSelectedProps] = useState<SelectedOption | null>(null);
  const [toggleModal, setToggleModal] = useState(false);
  const [validationError, setValidationError] = useState([]);
  const [formError, setFormError] = useState<string | null>(null);
  const [versionError, setVersionError] = useState<string | null>(null);
  const whenMounted = useWhenMounted();
  const { t } = useTranslation();

  //options Array -- TO DRY THE CODE
  const OPTIONS = [
    {
      optionName: ADD_GFLAG,
      title: (
        <>
          <span className="fa fa-plus" />
          {t('universeForm.gFlags.addGflags')}
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
          {t('universeForm.gFlags.addJson')}
        </>
      ),
      className: 'btn btn-default'
    }
  ];
  //server list - TO DRY THE CODE
  const SERVER_LIST = [
    {
      serverName: TSERVER,
      label: t('universeForm.gFlags.addToTServer')
    },
    {
      serverName: MASTER,
      label: t('universeForm.gFlags.addToMaster')
    }
  ];

  //handlers
  const handleSelectedOption = (serverProps: SelectedOption) => {
    setSelectedProps(serverProps);
    setToggleModal(true);
    setFormError(null);
  };

  const callValidation = async (flagArr: Gflag[]) => {
    try {
      const currentArr = fields;
      const duplicateArr = _.remove(currentArr, (e: any) => flagArr.some((f) => f.Name === e.Name));
      const transformedArr = flagArr.map((e) => {
        const dupEl = duplicateArr.find((f: any) => f.Name === e.Name) || {};
        return { ...dupEl, ...e };
      });
      const payload = { gflags: [...currentArr, ...transformedArr] };
      const validationResponse = await validateGFlags(dbVersion, payload);
      setValidationError(validationResponse?.data);
    } catch (e: any) {
      setVersionError(e?.error);
    }
  };

  const checkExistsAndPush = (flagObj: any) => {
    try {
      const allFields = fields;
      const fieldIndex = allFields?.findIndex((f: any) => f?.Name === flagObj?.Name);
      if (fieldIndex > -1) {
        remove(fieldIndex);
        insert(fieldIndex, { ...allFields[fieldIndex], ...flagObj });
      } else append(flagObj);
    } catch (e) {
      console.error(e);
    }
  };

  const handleFormSubmit = (values: any, actions: any) => {
    switch (values.option) {
      case FREE_TEXT: {
        try {
          const formValues = JSON.parse(values?.flagvalue);
          const newFlagArr: Gflag[] = [];
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
          setFormError(t('universeForm.gFlags.validationError'));
          setTimeout(() => {
            setFormError(null);
            actions.setSubmitting(false);
          }, 5000);
        }
        break;
      }

      case ADD_GFLAG: {
        const obj = { Name: values?.flagname, [values?.server]: values?.flagvalue };
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
          const payload = { gflags: fields };
          const validationResponse = await validateGFlags(dbVersion, payload);
          setValidationError(validationResponse?.data);
        } catch (e: any) {
          setVersionError(e?.error);
        }
      }
    });
  };

  useUpdateEffect(onVersionChange, [dbVersion]);

  const errorPopover = (title: string, msg: string) => (
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

  const nameFormatter = (cell: string, row: GFlagWithId, e: any, index: number) => {
    const eInfo: any = validationError?.find((e: GflagValidationResponse) => e.Name === cell); //error info
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
                  {t('universeForm.gFlags.removeFlag')}
                </Tooltip>
              }
            >
              <Button bsClass="flag-icon-button mb-2" onClick={() => remove(index)}>
                <img alt="Remove" src={Close} width="22" />
              </Button>
            </OverlayTrigger>
            &nbsp;
          </div>
          {flagDoesNotExists &&
            errorPopover(
              t('universeForm.gFlags.incorrectFlagName'),
              t('universeForm.gFlags.incorrectFlagMsg')
            )}
        </div>
      </div>
    );
  };

  const handleRemoveFlag = (rowObj: Gflag, i: number, serverType: SERVER, removeFlag: string) => {
    if (removeFlag) {
      remove(i);
      const newObj = _.pick(rowObj, ['Name', serverType === MASTER ? TSERVER : MASTER]);
      insert(i, newObj);
    } else remove(i);
  };

  const valueFormatter = (cell: string, row: GFlagWithId, index: number, server: SERVER) => {
    const valueExists = cell !== undefined;
    const eInfo: any = validationError?.find((e: GflagValidationResponse) => e.Name === row?.Name); //error info
    const isError = eInfo && eInfo[server]?.error;
    const isFlagExist = eInfo && eInfo[server]?.exist === true;
    const notExists = eInfo && eInfo[server]?.exist === false;

    let modalProps: SelectedOption = {
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

      const checkFlagExistsOnOtherServer = (serverType: SERVER) => {
        return (
          eInfo &&
          (eInfo[MASTER]?.exist === true || eInfo[TSERVER]?.exist === true) &&
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
                      {t('universeForm.gFlags.editFlag')}
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
                    {checkFlagExistsOnOtherServer(server)
                      ? t('universeForm.gFlags.removeValue')
                      : t('universeForm.gFlags.removeFlag')}
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
                t('universeForm.gFlags.incorrectFlagValue'),
                isFlagExist
                  ? eInfo[server]?.error
                  : t('universeForm.gFlags.flagNotBelongError', {
                      server
                    })
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
        <div className="table-val-column">
          {isFlagExist && (
            <Button
              bsClass="flag-icon-button display-inline-flex mb-2"
              onClick={() => handleSelectedOption(modalProps)}
            >
              <img alt="Add" src={Plus} width="20" />
              <span className="add-label">{t('universeForm.gFlags.addValue')}</span>
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
          data={fields}
          height={editMode ? '420px' : 'auto'}
          maxHeight="420px"
          tableStyle={{ overflow: 'scroll' }}
        >
          <TableHeaderColumn width="40%" dataField="Name" dataFormat={nameFormatter} isKey>
            <span className="header-title">{t('universeForm.gFlags.flagName')}</span>
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="TSERVER"
            width="30%"
            dataFormat={(cell, row, e, index) => valueFormatter(cell, row, index, TSERVER)}
          >
            <span className="header-title">{t('universeForm.gFlags.tServerValue')}</span>
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="MASTER"
            width="30%"
            dataFormat={(cell, row, e, index) => valueFormatter(cell, row, index, MASTER)}
          >
            <span className="header-title">{t('universeForm.gFlags.masterValue')}</span>
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    );
  };

  const renderOption = (formProps: any) => {
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
      flagvalue: Yup.mixed().required(t('universeForm.validation.fieldRequired'))
    });
    const modalTitle =
      selectedProps?.mode === CREATE
        ? selectedProps?.label
        : t('universeForm.gFlags.editFlagValue');
    const modalLabel =
      selectedProps?.mode === CREATE ? t('universeForm.gFlags.addFlag') : t('common.confirm');
    return (
      <YBModalForm
        title={modalTitle}
        visible={toggleModal}
        submitLabel={modalLabel}
        formName="ADDGFlagForm"
        cancelLabel={t('common.cancel')}
        error={formError}
        validationSchema={gflagSchema}
        showCancelButton={true}
        onHide={() => setToggleModal(false)}
        onFormSubmit={handleFormSubmit}
        render={(properties: any) => renderOption(properties)}
        dialogClassName={toggleModal ? 'gflag-modal modal-fade in' : 'modal-fade'}
        headerClassName="add-flag-header"
        showBackButton={true}
      />
    );
  };

  const renderBanner = () => {
    return (
      <div className="gflag-empty-banner">
        <span className="empty-text">{t('universeForm.gFlags.noFlags')}</span>
      </div>
    );
  };

  return (
    <Box display="flex" width="100%" flexDirection="column">
      {versionError && (
        <Alert bsStyle="danger">
          {versionError} ({t('universeForm.gFlags.selectedDBVersion')}
          <b>{dbVersion}</b>)
        </Alert>
      )}
      <Box flexShrink={1} flexDirection="row">
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
      </Box>
      {fields.length <= 0 && editMode && renderBanner()}
      {fields.length > 0 && renderTable()}
      {toggleModal && renderModal()}
    </Box>
  );
};
