/*
 * Created on Fri Apr 21 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

// Convert html document to text.

const ALLOWED_MARKUP_TAGS = ['STRONG', 'EM', 'U', 'S'];

export const convertHTMLToText = (HTMLstr: string) => {
  const htmlDoc = new DOMParser().parseFromString(HTMLstr, 'text/html');
  return normalizeHTMLTags(htmlDoc.body).join('\n');
};

const normalizeHTMLTags = (el: Element): string[] => {
  let children: any[] = Array.from(el.childNodes)
    .map((node) => normalizeHTMLTags(node as Element))
    .flat();

  if (ALLOWED_MARKUP_TAGS.includes(el.tagName)) {
    let tagName = el.tagName.toLowerCase();
    return [`<${tagName}>${children.join('')}</${tagName}>`];
  }

  if (el.tagName === 'SPAN') {
    return [el.textContent ?? ''];
  }

  if (!el.tagName) {
    return [el.textContent ?? ''];
  }

  if (el.tagName === 'P') {
    const align = el.getAttribute('align');
    if (align) {
      return [`<p align="${align}">${children.join('')}</p>`];
    }
    return [children.join('')];
  }

  return children;
};
