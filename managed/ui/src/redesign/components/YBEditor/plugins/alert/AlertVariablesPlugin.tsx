/*
 * Created on Fri Mar 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useCallback, useRef, useState } from 'react';

import clsx from 'clsx';
import pluralize from 'pluralize';
import { useClickAway } from 'react-use';
import { BaseSelection, Element, Text, Transforms } from 'slate';
import { ReactEditor, useSlate } from 'slate-react';
import { jsx } from 'slate-hyperscript';
import { useTranslation } from 'react-i18next';
import { Divider, makeStyles, Popover, Tooltip, Typography } from '@material-ui/core';

import {
  AlertVariableElement,
  CustomElement,
  CustomText,
  DOMElement,
  IYBEditor
} from '../custom-types';
import {
  CustomVariable,
  SystemVariables
} from '../../../../features/alerts/TemplateComposer/ICustomVariables';

import { IYBSlatePlugin, SlateRenderElementProps, SlateRenderLeafProps } from '../IPlugin';

import {
  deleteElement,
  deleteNChars,
  getBeforeNChars,
  nonActivePluginReturnType,
  Portal
} from '../PluginUtils';

import { AddAlertVariablesPopup } from '../../../../features/alerts/TemplateComposer/AlertVariablesPopup';
import { Close, Edit } from '@material-ui/icons';

const PLUGIN_NAME = 'Custom Alert Variables Plugin';
export const ALERT_VARIABLE_ELEMENT_TYPE = 'alertVariable';

export const ALERT_VARIABLE_START_TAG = '{{';
export const ALERT_VARIABLE_END_TAG = '}}';

const useStyle = makeStyles((theme) => ({
  alertVariable: {
    borderRadius: theme.spacing(0.5),
    color: theme.palette.secondary[700],
    fontWeight: 500,
    padding: theme.spacing(0.5),
    cursor: 'pointer',
    display: 'inline-flex',
    userSelect: 'none'
  },
  custom: {
    background: theme.palette.primary[200]
  },
  system: {
    background: theme.palette.secondary[100]
  },
  popoverHeader: {
    opacity: 0.4,
    marginBottom: theme.spacing(0.2)
  },
  popover: {
    padding: `${theme.spacing(0.5)}px ${theme.spacing(1.5)}px`,
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),
    height: '34px',
    color: 'rgba(35, 35, 41, 0.4)',
    textTransform: 'uppercase',
    fontSize: '10px',
    fontWeight: 500,
    '& svg': {
      padding: theme.spacing(0.25),
      borderRadius: theme.spacing(0.8),
      width: '20px',
      height: '20px',
      color: '#151730',
      cursor: 'pointer',
      '&:hover': {
        background: theme.palette.grey[100]
      }
    }
  },
  popoverTooltip: {
    '& .MuiTooltip-tooltip': {
      background: theme.palette.grey[900],
      color: theme.palette.common.white
    }
  }
}));

export const useAlertVariablesPlugin: IYBSlatePlugin = ({ editor, enabled }) => {
  const classes = useStyle();
  const [position, setPosition] = useState<DOMRect | null>(null);

  const alertPopupRef = useRef(null);

  useClickAway(alertPopupRef, () => {
    hidePopver();
  });

  const { t } = useTranslation();

  if (!enabled) {
    return { name: PLUGIN_NAME, ...nonActivePluginReturnType };
  }

  const hidePopver = () => {
    setPosition(null);
    // restore cursor position in editor
    const sel = editor.selection;
    Transforms.deselect(editor);
    if (sel) {
      ReactEditor.focus(editor);
      Transforms.select(editor, sel);
    }
  };

  const init = (editor: IYBEditor) => {
    const { isInline, isVoid, markableVoid } = editor;

    editor.isInline = (element: CustomElement) => {
      return ALERT_VARIABLE_ELEMENT_TYPE === element.type ? true : isInline(element);
    };

    editor.isVoid = (element: CustomElement) => {
      return ALERT_VARIABLE_ELEMENT_TYPE === element.type ? true : isVoid(element);
    };

    editor.markableVoid = (element: CustomElement) => {
      return ALERT_VARIABLE_ELEMENT_TYPE === element.type || markableVoid(element);
    };
  };

  /**
   * There are three modes(view) for alertVariable
   * 1) EDIT - can edit/delete variable
   * 2) PREVIEW - cannot delete/edit, used in preview mode.
   * 3) NO_VALUE - in preview mode, if no value is present, then this mode is used.
   */

  const renderElement = ({ attributes, children, element }: SlateRenderElementProps) => {
    if (element.type === ALERT_VARIABLE_ELEMENT_TYPE && element.view === 'EDIT') {
      return (
        <VariableDetailsPopover element={element} setPosition={setPosition}>
          <span
            contentEditable={false}
            className={clsx(
              classes.alertVariable,
              element.variableType === 'Custom' ? classes.custom : classes.system
            )}
            {...attributes}
          >
            {children}
            {ALERT_VARIABLE_START_TAG}
            {element.variableName}
            {ALERT_VARIABLE_END_TAG}
          </span>
        </VariableDetailsPopover>
      );
    } else if (
      element.type === ALERT_VARIABLE_ELEMENT_TYPE &&
      (element.view === 'PREVIEW' || element.view === 'NO_VALUE')
    ) {
      return (
        <Tooltip
          title={
            <>
              <Typography variant="subtitle1" className={classes.popoverHeader}>
                {pluralize.singular(
                  element.variableType === 'Custom'
                    ? t('alertCustomTemplates.customVariables.customVariables')
                    : t('alertCustomTemplates.customVariables.systemVariables')
                )}
              </Typography>
              <Typography variant="subtitle2">
                {element.view === 'PREVIEW'
                  ? element.variableName
                  : t('alertCustomTemplates.customVariablesPlugin.alertValueNotFound')}
              </Typography>
            </>
          }
          placement="top"
          arrow
        >
          <span
            contentEditable={false}
            className={clsx(
              classes.alertVariable,
              element.variableType === 'Custom' ? classes.custom : classes.system
            )}
            {...attributes}
          >
            {children}
            {element.variableValue ? element.variableValue : 'null'}
          </span>
        </Tooltip>
      );
    }
    // unsupported mode(view)
    else if (element.type === ALERT_VARIABLE_ELEMENT_TYPE) {
      throw new Error(`${PLUGIN_NAME} - no view type is specified`);
    }
    return undefined;
  };

  const onKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    // if the variable suggestion popover is opened,
    // hide the popover and delete the '{{' if present in the current selection.
    if (Boolean(position)) {
      hidePopver();
      if (getBeforeNChars(editor, ALERT_VARIABLE_START_TAG.length) === ALERT_VARIABLE_START_TAG) {
        deleteNChars(editor, ALERT_VARIABLE_START_TAG.length, true);
      }
      return true;
    }

    const { selection } = editor;
    const beforeChar = getBeforeNChars(editor, 1);

    //if last 2 characters is '{{', then open the popover
    if (selection && beforeChar && beforeChar + e.key === ALERT_VARIABLE_START_TAG) {
      const domRange = ReactEditor.toDOMRange(editor, selection);
      setPosition(domRange.getBoundingClientRect());
      //return true, because we handled it
      return true;
    }
    return false;
  };

  const renderLeaf = (_props: SlateRenderLeafProps) => {
    return undefined;
  };

  /**
   * add variables to the editor
   * @param variable variable to insert
   */
  const addAlertVariableElement = (variable: AlertVariableElement) => {
    //if preceding 2 chars is '{{', delete it
    if (getBeforeNChars(editor, ALERT_VARIABLE_START_TAG.length) === ALERT_VARIABLE_START_TAG) {
      deleteNChars(editor, ALERT_VARIABLE_START_TAG.length, true);
    }

    //insert the variable
    Transforms.insertNodes(editor, variable);

    //hide the variable suggesstion popover
    if (position) {
      setPosition(null);
    }

    // move the cursor after the variable, to let the user continue typing
    if (editor.selection) {
      Transforms.select(editor, editor.selection);
      Transforms.move(editor, { distance: 1 });
      ReactEditor.focus(editor);
    }
  };

  //attach helper functions to the editor
  editor['addAlertVariableElement'] = addAlertVariableElement;
  editor['addCustomVariable'] = (variable: CustomVariable) => {
    addAlertVariableElement({
      type: ALERT_VARIABLE_ELEMENT_TYPE,
      variableName: variable.name,
      variableValue: variable.defaultValue,
      view: 'EDIT',
      variableType: 'Custom',
      children: [{ text: '' }]
    });
  };
  editor['addSystemVariable'] = (variable: SystemVariables) => {
    addAlertVariableElement({
      type: ALERT_VARIABLE_ELEMENT_TYPE,
      variableName: variable.name,
      variableValue: '',
      view: 'EDIT',
      variableType: 'System',
      children: [{ text: '' }]
    });
  };

  return {
    name: PLUGIN_NAME,
    init,
    renderElement,
    onKeyDown,
    renderLeaf,
    isEnabled: () => enabled,

    // alertvariablePopup will be inject into the dom
    defaultComponents: [
      useCallback(() => {
        return (
          <Portal>
            {position ? (
              <div
                style={{
                  top: `${position.top + window.pageYOffset + 20}px`,
                  left: `${position.left + window.pageXOffset}px`,
                  position: 'absolute',
                  zIndex: '999'
                }}
                ref={alertPopupRef}
              >
                <AddAlertVariablesPopup
                  show={Boolean(position)}
                  onCustomVariableSelect={(variable) => {
                    editor['addCustomVariable'](variable);
                  }}
                  onSystemVariableSelect={(variable) => {
                    editor['addSystemVariable'](variable);
                  }}
                />
              </div>
            ) : null}
          </Portal>
        );
      }, [position])
    ],
    serialize,
    deSerialize
  };
};

const useStylesBootstrap = makeStyles((theme) => ({
  arrow: {
    '&::before': {
      color: theme.palette.grey[900],
      border: 'nonw'
    }
  },
  tooltip: {
    backgroundColor: theme.palette.grey[900],
    color: theme.palette.common.white,
    padding: `${theme.spacing(1.1)}px ${theme.spacing(1.5)}px`
  }
}));

interface VariableDetailsPopoverProps {
  element: AlertVariableElement;
  setPosition: (position: DOMRect) => void;
  children: React.ReactElement;
}

/**
 * popup to manage the variables, edit/delete
 */
const VariableDetailsPopover: FC<VariableDetailsPopoverProps> = ({
  element,
  setPosition,
  children
}) => {
  const { t } = useTranslation();
  const titleStyles = useStylesBootstrap();

  const [anchorEl, setAnchorEl] = useState<HTMLButtonElement | null>(null);
  const [editorSelection, setEditorSelection] = useState<BaseSelection>(null);
  const open = Boolean(anchorEl);

  const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
    setAnchorEl(event.currentTarget);
  };
  const handleClose = () => {
    setAnchorEl(null);
  };

  const editor = useSlate();
  const classes = useStyle();

  return (
    <>
      {React.cloneElement(children, {
        onClick: handleClick
      })}
      <Portal>
        <Popover
          id="alert_variables_popover"
          anchorEl={anchorEl}
          open={open}
          onClose={handleClose}
          TransitionProps={{
            onEnter: () => {
              setEditorSelection(editor.selection);
            },
            onExit: () => {
              if (editorSelection) {
                ReactEditor.focus(editor);
                Transforms.setSelection(editor, editorSelection);
              }
            }
          }}
          anchorOrigin={{
            vertical: 'top',
            horizontal: 'right'
          }}
          transformOrigin={{
            vertical: 'bottom',
            horizontal: 'left'
          }}
        >
          <div className={classes.popover}>
            {!ReactEditor.isReadOnly(editor) && (
              <>
                <Tooltip
                  title={t('common.edit') as string}
                  placement="top"
                  arrow
                  classes={titleStyles}
                >
                  <Edit
                    onClick={() => {
                      // onEdit, delete the element and show the popover
                      deleteElement(editor, element);
                      const { selection } = editor;
                      if (selection) {
                        const domRange = ReactEditor.toDOMRange(editor, selection);
                        setPosition(domRange.getBoundingClientRect());
                      }
                    }}
                  />
                </Tooltip>
                <Tooltip
                  title={t('common.remove') as string}
                  placement="top"
                  arrow
                  classes={titleStyles}
                >
                  <Close
                    onClick={() => {
                      deleteElement(editor, element);
                    }}
                  />
                </Tooltip>
                <Divider orientation="vertical" flexItem />
              </>
            )}

            {t('alertCustomTemplates.customVariablesPlugin.sysOrCusVariable', {
              variable_type: element.variableType
            })}
          </div>
        </Popover>
      </Portal>
    </>
  );
};

// serialize AlertVariableElement to html string
const serialize = (node: CustomElement | CustomText, children: string) => {
  // we don't support text level
  if (Text.isText(node)) {
    return undefined;
  }

  if (node.type === ALERT_VARIABLE_ELEMENT_TYPE) {
    return `<span type="${node.type}" variableType="${node.variableType}" variableName="${node.variableName}">${ALERT_VARIABLE_START_TAG}${node.variableName}${ALERT_VARIABLE_END_TAG}${children}</span>`;
  }

  return undefined;
};

// de-serialize htmlstring to AlertVariableElement
const deSerialize = (
  el: DOMElement,
  _markAttributes = {},
  children: Element[] | Node[]
): CustomElement | CustomText | undefined => {
  //we don't handle text nodes
  if (el.nodeType !== Node.ELEMENT_NODE) {
    return undefined;
  }

  if (el.nodeName === 'SPAN' && el.getAttribute('type') === ALERT_VARIABLE_ELEMENT_TYPE) {
    return jsx(
      'element',
      {
        type: ALERT_VARIABLE_ELEMENT_TYPE,
        variableType: el.getAttribute('variableType'),
        variableName: el.getAttribute('variableName'),
        variableValue: el.textContent,
        view: 'EDIT'
      },
      [{ text: '' }]
    );
  }
  return undefined;
};
