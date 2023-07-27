import { FC, useState } from 'react';
import { FieldArray } from 'formik';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, InputAdornment, IconButton } from '@material-ui/core';
import { DragDropContext, Droppable, Draggable } from 'react-beautiful-dnd';
import { YBButton, YBInput } from '../../../redesign/components';
import { TSERVER } from '../../../redesign/features/universe/universe-form/form/fields';
import { isNonEmptyArray, isNonEmptyString } from '../../../utils/ObjectUtils';
import {
  CONST_VALUES,
  GFLAG_EDIT,
  formatLDAPConf,
  verifyLDAPAttributes,
  unformatLDAPConf,
  MultilineGFlags
} from '../../../utils/UniverseUtils';
import { ReactComponent as DraggableIcon } from '../../../redesign/assets/draggable.svg';
import { ReactComponent as CloseIcon } from '../../../redesign/assets/close.svg';

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
      color: 'orange'
    }
  }
}));

interface EditGFlagConfProps {
  formProps: any;
  mode: string;
  serverType: string;
  flagName: string;
}

export interface GFlagRowProps {
  id: string;
  index: number;
  content: string;
  error: boolean;
  errorMessage?: string;
  isWarning?: boolean;
}

const getGFlagRows = (rowCount: number) =>
  Array.from({ length: rowCount }, (v, k) => k).map((k) => ({
    id: `item-${k}`,
    index: k,
    content: CONST_VALUES.EMPTY_STRING,
    error: false,
    errorMessage: '',
    isWarning: false
  }));

const reorderGFlagRows = (GFlagRows: any, startIndex: number, endIndex: number) => {
  let result = Array.from(GFlagRows);
  const [removed] = result.splice(startIndex, 1);
  result.splice(endIndex, 0, removed);
  result = result.filter((data) => data !== undefined);
  return result;
};

export const EditGFlagsConf: FC<EditGFlagConfProps> = ({
  formProps,
  mode,
  serverType,
  flagName
}) => {
  let unformattedLDAPConf: string | null = null;
  const GFlagConfData = CONST_VALUES.EMPTY_STRING;
  const { t } = useTranslation();
  const classes = useStyles();

  // Define state variables
  const GFlagValueConfObject =
    serverType === TSERVER
      ? formProps?.values?.tserverFlagDetails?.flagvalueobject
      : formProps?.values?.masterFlagDetails?.flagvalueobject;

  if (mode === GFLAG_EDIT && !GFlagValueConfObject) {
    unformattedLDAPConf = unformatLDAPConf(formProps?.values?.flagvalue);
  }
  const [rowCount, setRowCount] = useState<number>(
    GFlagValueConfObject?.length > 0 ? GFlagValueConfObject?.length : 2
  );
  const [errorMessage, setErrorMessage] = useState<string>(CONST_VALUES.EMPTY_STRING);
  const [GFlagRows, setGFlagConfRows] = useState<any>(
    GFlagValueConfObject?.length > 0
      ? GFlagValueConfObject
      : mode === GFLAG_EDIT && isNonEmptyString(formProps?.values?.flagvalue)
      ? unformattedLDAPConf
      : getGFlagRows(rowCount)
  );

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

  const addItem = (rowCount: number) => {
    GFlagRows.push({
      id: `item-${rowCount - 1}`,
      index: rowCount - 1,
      content: CONST_VALUES.EMPTY_STRING,
      error: false,
      errorMessage: '',
      isWarning: false
    });
    return GFlagRows;
  };

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

  const buildGFlagConf = (arrangedRows?: any) => {
    const rows = arrangedRows ?? GFlagRows;
    let isGFlagInvalid = false;
    const filteredRows = rows?.filter((GFlagRow: GFlagRowProps) => {
      isGFlagInvalid = isGFlagInvalid || GFlagRow.error;
      return isNonEmptyString(GFlagRow.content);
    });

    const confData = filteredRows?.reduce((accumulator: string, GFlagRow: any, index: number) => {
      let appendChar = CONST_VALUES.COMMA_SEPARATOR;
      const endChar = CONST_VALUES.EMPTY_STRING;
      let GFlagRowContent = GFlagRow.content;

      if (index === 0) {
        appendChar = CONST_VALUES.EMPTY_STRING;
      }
      if (GFlagRowContent.includes(CONST_VALUES.LDAP)) {
        GFlagRowContent = `"${formatLDAPConf(GFlagRowContent)}"`;
      }

      return accumulator + appendChar + GFlagRowContent + endChar;
    }, GFlagConfData);

    const serverTypeProp = serverType === TSERVER ? 'tserverFlagDetails' : 'masterFlagDetails';
    const flagDetails = {
      previewFlagValue: confData,
      flagvalueobject: rows,
      previewFlagError: errorMessage
    };

    formProps.setFieldValue(serverTypeProp, flagDetails);
    formProps.setFieldValue('flagvalue', isGFlagInvalid ? CONST_VALUES.EMPTY_STRING : confData);
  };

  // Triggered when users enters any value in the text box
  const handleChange = (GFlagInput: string, index: number) => {
    const { isErrorInValidation, errorMessage, isWarning } = validateInput(GFlagInput);

    // Set row in error state if isErrorInValidation is true
    isErrorInValidation ? (GFlagRows[index].error = true) : (GFlagRows[index].error = false);
    // Set row in warning state if isWarning is true
    GFlagRows[index].isWarning = isWarning;
    GFlagRows[index].errorMessage = errorMessage;
    isWarning || isNonEmptyString(errorMessage)
      ? setErrorMessage(errorMessage)
      : setErrorMessage(CONST_VALUES.EMPTY_STRING);

    GFlagRows[index].content = GFlagInput;
  };

  const validateInput = (GFlagInput: string) => {
    const { isAttributeInvalid, errorMessage, isWarning } = verifyLDAPAttributes(GFlagInput);

    return {
      isErrorInValidation: isAttributeInvalid,
      errorMessage,
      isWarning
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
                            <Box display="flex" flexDirection="row">
                              <YBInput
                                name={`flagvalue.${index}`}
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
                                helperText={t(GFlagRows[index]?.errorMessage)}
                                inputProps={{
                                  'data-testid': `UniverseGFlagsConfField-item${index}`
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
                                onClick={() => {
                                  removeItem(index);
                                }}
                              >
                                <CloseIcon />
                              </IconButton>
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
        data-testid={`UniverseGFlagsConfField-AddRowButton`}
      >
        {t('universeForm.gFlags.addRow')}
      </YBButton>
    </Box>
  );
};
