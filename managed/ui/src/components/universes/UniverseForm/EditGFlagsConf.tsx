import { FC, useEffect, useState } from 'react';
import { FieldArray } from 'formik';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, InputAdornment, IconButton, Divider } from '@material-ui/core';
import { DragDropContext, Droppable, Draggable } from 'react-beautiful-dnd';
import { YBButton, YBInput } from '../../../redesign/components';
import { TSERVER } from '../../../redesign/features/universe/universe-form/form/fields';
import { ManageGFlagJWKS } from './ManageGFlagJWKS';
import { PreviewGFlagJWKS } from './PreviewGFlagJWKS';
import { isEmptyString, isNonEmptyArray, isNonEmptyString } from '../../../utils/ObjectUtils';
import {
  CONST_VALUES,
  formatConf,
  MultilineGFlags,
  unformatConf,
  verifyAttributes
} from '../../../utils/UniverseUtils';
import { compareYBSoftwareVersions } from '../../../utils/universeUtilsTyped';
import { ReactComponent as DraggableIcon } from '../../../redesign/assets/draggable.svg';
import { ReactComponent as CloseIcon } from '../../../redesign/assets/close.svg';

const OIDC_ENABLED_DB_VERSION = '2.18.0.0';

const useStyles = makeStyles((theme) => ({
  numNodesInputField: {
    overflow: 'auto',
    maxHeight: '206px'
  },
  gflagBox: {
    marginBottom: theme.spacing(2)
  },
  draggableIcon: {
    marginLeft: theme.spacing(1),
    cursor: 'pointer'
  },
  overrideMuiInput: {
    '& .MuiInput-root.MuiInput-multiline': {
      padding: 4
    }
  },
  overrideMuiHelperText: {
    '& .MuiFormHelperText-root': {
      color: theme.palette.orange[500]
    }
  },
  updateKeySetInput: {
    '& .MuiInput-root': {
      background: '#EBEBEB'
    }
  },
  editKeySetImage: {
    cursor: 'pointer'
  },
  divider: {
    border: '1px',
    marginTop: theme.spacing(2),
    backgroundColor: '#E5E5E9'
  },
  iconButton: {
    marginBottom: theme.spacing(3)
  }
}));

interface EditGFlagConfProps {
  formProps: any;
  dbVersion: string;
  serverType: string;
  updateJWKSDialogStatus: (status: boolean) => void;
}

export interface GFlagRowProps {
  id: string;
  index: number;
  content: string;
  error: boolean;
  errorMessageKey?: string;
  isWarning?: boolean;
  showJWKSButton?: boolean;
  JWKSToken?: any;
}

export const getSearchTerm = (GFlagRowContent: string) => {
  let searchTerm = null;

  if (GFlagRowContent.includes(CONST_VALUES.LDAP)) {
    searchTerm = CONST_VALUES.LDAP;
  } else if (GFlagRowContent.includes(CONST_VALUES.JWT)) {
    searchTerm = CONST_VALUES.JWT;
  }
  return searchTerm;
};

const getGFlagRows = (rowCount: number) =>
  Array.from({ length: rowCount }, (v, k) => k).map((k) => ({
    id: `item-${k}`,
    index: k,
    content: CONST_VALUES.EMPTY_STRING,
    error: false,
    errorMessageKey: CONST_VALUES.EMPTY_STRING,
    isWarning: false
  }));

const reorderGFlagRows = (GFlagRows: GFlagRowProps[], startIndex: number, endIndex: number) => {
  let result = Array.from(GFlagRows);
  const [removed] = result.splice(startIndex, 1);
  result.splice(endIndex, 0, removed);
  result = result.filter((data) => data !== undefined);
  return result;
};

export const EditGFlagsConf: FC<EditGFlagConfProps> = ({
  formProps,
  dbVersion,
  serverType,
  updateJWKSDialogStatus
}) => {
  let unformattedLDAPConf: GFlagRowProps[] | null = null;
  const GFlagConfData = CONST_VALUES.EMPTY_STRING;
  const { t } = useTranslation();
  const classes = useStyles();

  // Define state variables
  const GFlagValueConfObject =
    serverType === TSERVER
      ? formProps?.values?.tserverFlagDetails?.flagvalueobject
      : formProps?.values?.masterFlagDetails?.flagvalueobject;
  const flagValue = formProps?.values?.flagvalue;
  const versionDifference = compareYBSoftwareVersions(dbVersion, OIDC_ENABLED_DB_VERSION);
  const isOIDCSupported = versionDifference >= 0;

  if (isNonEmptyString(flagValue) && !GFlagValueConfObject) {
    unformattedLDAPConf = unformatConf(flagValue);
  }

  const [flagName, setFlagName] = useState<string>(formProps?.values?.flagname);
  const [showJWKSButton, setShowJWKSButton] = useState<boolean>(false);
  const [showJWKSDialog, setShowJWKSDialog] = useState<boolean>(false);
  const [JWKSKey, setJWKSToken] = useState<string>(CONST_VALUES.EMPTY_STRING);
  const [JWKSDialogIndex, setJWKSDialogIndex] = useState<number>(-1);
  const [rowCount, setRowCount] = useState<number>(
    GFlagValueConfObject?.length > 0 ? GFlagValueConfObject?.length : 1
  );
  const [errorMessageKey, setErrorMessageKey] = useState<string>(CONST_VALUES.EMPTY_STRING);
  const [GFlagRows, setGFlagConfRows] = useState<GFlagRowProps[]>(
    GFlagValueConfObject?.length > 0
      ? GFlagValueConfObject
      : isNonEmptyString(flagValue)
      ? unformattedLDAPConf
      : getGFlagRows(rowCount)
  );

  useEffect(() => {
    if (!GFlagValueConfObject && isNonEmptyString(flagValue)) {
      setGFlagConfRows(unformattedLDAPConf!);
    }
  }, [flagValue]);

  useEffect(() => {
    setFlagName(formProps?.values?.flagname);
  }, [formProps?.values?.flagname]);

  const getPlaceholder = (index: number, flagName: string) => {
    if (flagName === MultilineGFlags.YSQL_IDENT_CONF_CSV) {
      return 'universeForm.gFlags.identConfLocal';
    }

    let message = CONST_VALUES.EMPTY_STRING;
    switch (index) {
      case 0:
        message = 'universeForm.gFlags.hbaLDAPConfLocal';
        break;
      case 1:
        message = 'universeForm.gFlags.hbaLDAPConfHost';
        break;
      default:
        message = 'universeForm.gFlags.hbaLDAPConfHostSSL';
        break;
    }
    return message;
  };

  // Adds additional rows
  const addItem = (rowCount: number) => {
    GFlagRows.push({
      id: `item-${rowCount - 1}`,
      index: rowCount - 1,
      content: CONST_VALUES.EMPTY_STRING,
      error: false,
      errorMessageKey: CONST_VALUES.EMPTY_STRING,
      isWarning: false
    });
    return GFlagRows;
  };

  // Removes rows
  const removeItem = (index: number) => {
    GFlagRows.splice(index, 1);
    setGFlagConfRows(GFlagRows);
    setRowCount(rowCount - 1);
    buildGFlagConf(GFlagRows);
  };

  const handleRowClick = () => {
    const addedRow = addItem(rowCount + 1);
    setGFlagConfRows(addedRow);
    setRowCount(rowCount + 1);
  };

  // Helper function which builds the flag value for multiline CSV
  const buildGFlagConf = (arrangedRows?: any) => {
    const rows = arrangedRows ?? GFlagRows;
    let isGFlagInvalid = false;
    const filteredRows = rows?.filter((GFlagRow: GFlagRowProps) => {
      isGFlagInvalid = isGFlagInvalid || GFlagRow.error;
      return isNonEmptyString(GFlagRow.content);
    });

    const confData = filteredRows?.reduce(
      (accumulator: string, GFlagRow: GFlagRowProps, index: number) => {
        let appendChar = CONST_VALUES.COMMA_SEPARATOR;
        const endChar = CONST_VALUES.EMPTY_STRING;
        const GFlagRowContent = GFlagRow.content;
        const GFlagRowJWKSKey = GFlagRow.JWKSToken;
        let formattedConf = CONST_VALUES.EMPTY_STRING;

        if (index === 0) {
          appendChar = CONST_VALUES.EMPTY_STRING;
        }
        if (
          GFlagRowContent.includes(CONST_VALUES.LDAP) ||
          GFlagRowContent.includes(CONST_VALUES.JWT)
        ) {
          const searchTerm = getSearchTerm(GFlagRowContent);
          formattedConf = `"${formatConf(
            isEmptyString(formattedConf) ? GFlagRowContent : formattedConf,
            searchTerm,
            GFlagRowContent.includes(CONST_VALUES.JWT) && GFlagRowJWKSKey ? GFlagRowJWKSKey : ''
          )}"`;
        }

        return (
          accumulator +
          appendChar +
          (isEmptyString(formattedConf) ? GFlagRowContent : formattedConf) +
          endChar
        );
      },
      GFlagConfData
    );

    const serverTypeProp = serverType === TSERVER ? 'tserverFlagDetails' : 'masterFlagDetails';
    const flagDetails = {
      previewFlagValue: confData,
      flagvalueobject: rows
    };
    const JWKSDetails = {
      showJWKSButton,
      JWKSKey,
      errorMessageKey
    };

    formProps.setFieldValue('JWKSDetails', JWKSDetails);
    formProps.setFieldValue(serverTypeProp, flagDetails);
    formProps.setFieldValue('flagvalue', isGFlagInvalid ? CONST_VALUES.EMPTY_STRING : confData);
  };

  // Triggered when users enters any value in each of the row
  const handleChange = (GFlagInput: string, index: number) => {
    const JWKSKeyset = GFlagRows[index].JWKSToken;
    const { isErrorInValidation, errorMessageKey, isWarning, searchTerm } = validateInput(
      GFlagInput,
      JWKSKeyset
    );

    // Set row in error state if isErrorInValidation is true
    GFlagRows[index].error = isErrorInValidation;
    // Set row in warning state if isWarning is true
    GFlagRows[index].isWarning = isWarning;
    GFlagRows[index].errorMessageKey = errorMessageKey;

    isWarning || isNonEmptyString(errorMessageKey)
      ? setErrorMessageKey(errorMessageKey)
      : setErrorMessageKey(CONST_VALUES.EMPTY_STRING);
    GFlagRows[index].content = GFlagInput;
    GFlagRows[index].showJWKSButton = searchTerm === CONST_VALUES.JWT;
    if (searchTerm !== CONST_VALUES.JWT) {
      GFlagRows[index].JWKSToken = CONST_VALUES.EMPTY_STRING;
    }

    setShowJWKSButton(searchTerm === CONST_VALUES.JWT);
  };

  // Gets triggered on entry of characters in each row
  const validateInput = (GFlagInput: string, JWKSKeyset: string) => {
    const searchTerm = getSearchTerm(GFlagInput);
    const { isAttributeInvalid, errorMessageKey, isWarning } = verifyAttributes(
      GFlagInput,
      searchTerm,
      JWKSKeyset,
      isOIDCSupported
    );
    return {
      isErrorInValidation: isAttributeInvalid,
      errorMessageKey,
      isWarning,
      searchTerm
    };
  };

  // The onDragEnd event occurs when a user has finished dragging a selection.
  const onDragEnd = (result: any) => {
    if (!result.destination) {
      return;
    }

    const reorderedItems = reorderGFlagRows(
      GFlagRows,
      result.source.index,
      result.destination.index
    );

    setGFlagConfRows(reorderedItems);
    buildGFlagConf(reorderedItems);
  };

  const setRowJWKSToken = (index: number, key: string) => {
    GFlagRows[index].JWKSToken = key;

    setGFlagConfRows(GFlagRows);
    // Handle validation to ensure upload file content is added to flag content
    handleChange(GFlagRows[index].content, index);
    // Build the conf csv string after file is uploaded
    buildGFlagConf(GFlagRows);
    setJWKSToken(key);
  };

  // Opens JWKS modal dialog for a specific row and allows
  // user to enter the JWKS token
  const openJWKSDialog = (index: number) => {
    updateJWKSDialogStatus(true);
    setShowJWKSDialog(true);
    setJWKSDialogIndex(index);
  };

  // Removed the JWKS token for a sprcific row
  const removeJWKSToken = (index: number) => {
    GFlagRows[index].JWKSToken = CONST_VALUES.EMPTY_STRING;
    handleChange(GFlagRows[index].content, index);
    setJWKSToken(CONST_VALUES.EMPTY_STRING);
  };

  return (
    <Box className={classes.numNodesInputField}>
      <DragDropContext onDragEnd={onDragEnd} key={serverType}>
        <Droppable droppableId="droppable">
          {(provided, snapshot) => (
            <div {...provided.droppableProps} ref={provided.innerRef}>
              <FieldArray
                name="flagvalue"
                render={() => (
                  <>
                    {/* eslint-disable-next-line react/display-name */}
                    {GFlagRows?.map((item: any, index: number) => (
                      <Draggable key={item.id} draggableId={item.id} index={index}>
                        {(provided, snapshot) => (
                          <div
                            className={classes.gflagBox}
                            ref={provided.innerRef}
                            {...provided.draggableProps}
                            {...provided.dragHandleProps}
                          >
                            <Box display="flex" flexDirection="column">
                              <Box display="flex" flexDirection="row">
                                <YBInput
                                  key={`${flagName}`}
                                  name={`${flagName}-${index}`}
                                  id={`${index}`}
                                  fullWidth
                                  multiline={true}
                                  minRows={1}
                                  maxRows={8}
                                  placeholder={t(getPlaceholder(item.index, flagName))}
                                  defaultValue={
                                    isNonEmptyArray(GFlagValueConfObject) ||
                                    isNonEmptyArray(unformattedLDAPConf)
                                      ? GFlagRows[index].content
                                      : CONST_VALUES.EMPTY_STRING
                                  }
                                  onChange={(e: any) => handleChange(e.target.value, index)}
                                  onBlur={() => buildGFlagConf()}
                                  error={GFlagRows[index]?.error}
                                  helperText={t(GFlagRows[index].errorMessageKey!)}
                                  inputProps={{
                                    'data-testid': `EditMultilineConfField-row-${index}`
                                  }}
                                  className={clsx(classes.overrideMuiInput, {
                                    [classes.overrideMuiHelperText]: GFlagRows[index]?.isWarning
                                  })}
                                  InputProps={{
                                    startAdornment: (
                                      <Box className={classes.draggableIcon}>
                                        <InputAdornment position="start">
                                          <DraggableIcon />
                                        </InputAdornment>
                                      </Box>
                                    )
                                  }}
                                />
                                <IconButton
                                  className={
                                    GFlagRows[index].errorMessageKey ? classes.iconButton : ''
                                  }
                                  onClick={() => {
                                    removeItem(index);
                                  }}
                                >
                                  <CloseIcon />
                                </IconButton>
                              </Box>

                              {GFlagRows[index].showJWKSButton &&
                                (isEmptyString(GFlagRows[index].JWKSToken) ||
                                  !GFlagRows[index].JWKSToken) && (
                                  <YBButton
                                    key={`AddKeySet-row-${index}`}
                                    id={`AddKeySet-row-${index}`}
                                    variant="secondary"
                                    style={{ width: '254px', marginTop: '8px' }}
                                    onClick={() => {
                                      updateJWKSDialogStatus(true);
                                      setShowJWKSDialog(true);
                                      setJWKSDialogIndex(index);
                                    }}
                                    disabled={!isOIDCSupported}
                                    type="button"
                                    size="medium"
                                    data-testid={`EditMultilineConfField-AddKeySet-${index}`}
                                  >
                                    <i className="fa fa-plus" />
                                    {t('universeForm.gFlags.addKeyset')}
                                  </YBButton>
                                )}
                              {isNonEmptyString(GFlagRows[index].JWKSToken) && (
                                <Box mt={1} mr={4}>
                                  <PreviewGFlagJWKS
                                    JWKSKeyset={GFlagRows[index].JWKSToken}
                                    rowIndex={index}
                                    openJWKSDialog={openJWKSDialog}
                                    removeJWKSToken={removeJWKSToken}
                                  />
                                </Box>
                              )}
                              {index === JWKSDialogIndex && showJWKSDialog && (
                                <Box mt={2}>
                                  <ManageGFlagJWKS
                                    onHide={() => {
                                      updateJWKSDialogStatus(false);
                                      setShowJWKSDialog(false);
                                    }}
                                    open={showJWKSDialog}
                                    token={GFlagRows[index].JWKSToken}
                                    rowIndex={index}
                                    onUpdate={setRowJWKSToken}
                                  />
                                </Box>
                              )}
                              <Divider className={classes.divider} />
                            </Box>
                          </div>
                        )}
                      </Draggable>
                    ))}
                  </>
                )}
              ></FieldArray>
            </div>
          )}
        </Droppable>
      </DragDropContext>

      <YBButton
        variant="primary"
        onClick={handleRowClick}
        size="medium"
        data-testid={`EditMultilineConfField-AddRowButton`}
      >
        {t('universeForm.gFlags.addRow')}
      </YBButton>
    </Box>
  );
};
