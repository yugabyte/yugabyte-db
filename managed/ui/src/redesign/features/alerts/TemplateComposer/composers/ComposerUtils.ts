/*
 * Created on Fri Apr 21 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { uniq } from 'lodash';
import { HistoryEditor } from 'slate-history';
import { Editor, Element, Transforms } from 'slate';
import { Path } from 'slate';
import {
  ALERT_VARIABLE_ELEMENT_TYPE,
  AlertVariableElement,
  DefaultElement,
  IYBEditor,
  MATCH_ALL_BETWEEN_BRACKET_REGEX,
  clearEditor
} from '../../../../components/YBEditor/plugins';
import { IAlertVariablesList } from '../ICustomVariables';
import { TextToHTMLTransform } from '../../../../components/YBEditor/transformers/TextToHTMLTransform';
import { HTMLDeSerializer } from '../../../../components/YBEditor/serializers';

export  function findInvalidVariables(documentStr: string, savedVariables: IAlertVariablesList) {
  try {
    const variablesInDocument = documentStr.match(MATCH_ALL_BETWEEN_BRACKET_REGEX);

    if (!variablesInDocument) {
      return [];
    }
    // remove '{{' '}}' from the variables
    const alertVariables = variablesInDocument.map((m) => m.substring(2, m.length - 2).trim());

    const validVariables = [
      ...savedVariables.customVariables.map((v) => v.name),
      ...savedVariables.systemVariables.map((v) => v.name)
    ];
    return uniq(alertVariables.filter((v) => !validVariables.includes(v)));
  } catch (e) {
    return [];
  }
}

/**
 *
 * @param templateStr the template string
 * @param alertVariables list of alert variables both custom and system variable
 * @param editor the editor to load
 * This function transforms the plain text to HTML. then it transforms the html to slatenode and load it to the editor
 */
export function loadTemplateIntoEditor(
  templateStr = '',
  alertVariables: IAlertVariablesList,
  editor: IYBEditor | null
) {
  if (!editor) return;

  const htmlTemplate = TextToHTMLTransform(templateStr, alertVariables);
  try {
    let bodyVal = new HTMLDeSerializer(editor, htmlTemplate).deserialize();
    if (bodyVal[0].text !== undefined) {
      bodyVal = [
        {
          ...DefaultElement,
          children: bodyVal as any
        }
      ];
    }
    // Don't aleter the history while loading the template
    HistoryEditor.withoutSaving(editor, () => {
      clearEditor(editor);
      Transforms.insertNodes(editor, bodyVal);
    });
  } catch (e) {
    console.log(e);
  }
}

/**
 * fill the alert variables with the values. Used while previewing
 */
export const fillAlertVariablesWithValue = (editor: IYBEditor, text: string) => {
  const contents = new HTMLDeSerializer(editor, text).deserialize();

  clearEditor(editor);
  Transforms.insertNodes(editor, contents);

  let alertElements = Array.from(
    Editor.nodes(editor, {
      at: [],
      match: (node) => Element.isElement(node) && node.type === ALERT_VARIABLE_ELEMENT_TYPE //get only ALERT_VARIABLE_ELEMENT
    })
  );

  alertElements.forEach((next) => {
    const [_, path] = next as [AlertVariableElement, Path];
    //set the mode to preview such that it display the value, instead of variable name
    Transforms.setNodes(editor, { view: 'PREVIEW' }, { at: path });
  });
};
