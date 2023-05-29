/*
 * Created on Fri Apr 21 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { replace } from 'lodash';
import {
  ALERT_VARIABLE_ELEMENT_TYPE,
  ALERT_VARIABLE_END_TAG,
  ALERT_VARIABLE_START_TAG,
  MATCH_ALL_BETWEEN_BRACKET_REGEX
} from '../plugins';
import { IAlertVariablesList } from '../../../features/alerts/TemplateComposer/ICustomVariables';

type IVariableList = {
  custom: string[];
  system: string[];
};

/**
 * replaces the variables within quotes with the html syntax , which will be converted into slatejs node
 * @param line
 * @param variablesList list of variables. used to determine the type of variable
 */
const replaceVariablesWithHTMLSyntax = (line: string, variablesList: IVariableList) => {
  return replace(line, MATCH_ALL_BETWEEN_BRACKET_REGEX, (v) => {
    const variableName = v.substring(2, v.length - 2).trim();
    const variableType = variablesList.custom.includes(variableName)
      ? 'Custom'
      : variablesList.system.includes(variableName)
      ? 'System'
      : '';

    return `<span variableType="${variableType}" type="${ALERT_VARIABLE_ELEMENT_TYPE}"  variableName="${variableName}" >${ALERT_VARIABLE_START_TAG}${variableName}${ALERT_VARIABLE_END_TAG}</span>`;
  });
};

/**
 * Transforms normal text into html fragments
 */

export function TextToHTMLTransform(text: string, alertVariables: IAlertVariablesList) {
  try {
    const variablesList = {
      custom: alertVariables.customVariables.map((v) => v.name),
      system: alertVariables.systemVariables.map((v) => v.name)
    };

    // split the text by \n and parse each line.
    // each line contains a list of elements
    const domElementsByLine = text.split('\n').map((line) => {
      return Array.from(new DOMParser().parseFromString(line, 'text/html').body.childNodes);
    });

    let output = '';

    for (const elements of domElementsByLine as Element[][]) {
      let innerContents = '';

      for (const element of elements) {
        if (element.tagName) {
          // element is not text, it has tags like P, SPAN etc

          if (element.tagName === 'P') {
            const align = element.getAttribute('align');

            if (align) {
              innerContents += `<p align="${align}">${replaceVariablesWithHTMLSyntax(
                element.innerHTML ?? '',
                variablesList
              )}</p>`;
            }
          } else {
            const tagName = element.tagName.toLowerCase();
            innerContents +=
              `<${tagName}>` +
              replaceVariablesWithHTMLSyntax(element.innerHTML ?? '', variablesList) +
              `</${tagName}>`;
          }
        } else {
          innerContents += replaceVariablesWithHTMLSyntax(element.textContent ?? '', variablesList);
        }
      }
      // if the line is already enclosed by P tag, skip appendig the p tag
      if (elements[0]?.tagName === 'P' && elements[0]?.getAttribute('align')) {
        output += innerContents;
      } else {
        output += `<p>${innerContents}</p>`;
      }
    }
    return output;
  } catch (e) {
    console.error(e);
    return '';
  }
}
