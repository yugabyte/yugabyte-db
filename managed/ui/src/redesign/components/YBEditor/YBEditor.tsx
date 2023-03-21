/*
 * Created on Thu Mar 02 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */


import React, { FC, useCallback, useMemo } from 'react'
import clsx from 'clsx'
import { BaseEditor, createEditor, Descendant } from 'slate'
import { Slate, Editable, withReact } from 'slate-react'
import { withHistory } from 'slate-history'
import { Grid, makeStyles } from '@material-ui/core'
import { useEditorPlugin } from './plugins/PluginManager'
import { TextDecorators } from './plugins/custom-types'
import { toggleMark } from './plugins/PluginUtils'
import { FormatBold, FormatItalic, FormatStrikethrough, FormatUnderlined } from '@material-ui/icons'

const initialValue: Descendant[] = [
  {
    type: 'paragraph',
    children: [{ text: '' }],
  },
]

const ToolbarIcons: Record<TextDecorators, any> = {
  italic: {
    icon: <FormatItalic />
  },
  bold: {
    icon: <FormatBold />
  },
  underline: {
    icon: <FormatUnderlined />
  },
  strikethrough: {
    icon: <FormatStrikethrough />
  }
}

interface YBEditorProps {
  setVal: (val: Descendant[]) => void;
  editorProps?: BaseEditor
  showToolbar?: boolean;
  singleLine?: boolean;
}

const useStyles = makeStyles((theme) => ({
  root: {
    border: '1px solid #E5E5E6',
    background: theme.palette.common.white,
    borderRadius: `${theme.spacing(1)}px ${theme.spacing(1)}px 0px 0px`,
  },
  rootSingleLine: {
    borderRadius: theme.spacing(1)
  },
  editable: {
    height: '380px',
    padding: theme.spacing(2),
    overflowY: 'auto'
  },
  singleLine: {
    height: theme.spacing(4),
    padding: theme.spacing(0.5),
    overflowY: 'hidden'
  },
  toolbarRoot: {
    height: theme.spacing(5.2),
    display: 'flex',
    justifyContent: 'space-between',
    background: '#FAFBFC',
    borderBottom: `1px solid ${theme.palette.ybacolors.ybGrayHover}`
  },
  formatIcons: {
    display: 'flex',
    alignItems: 'center',
    padding: theme.spacing(1),
    "& > svg": {
      width: theme.spacing(3),
      height: theme.spacing(3)
    }
  }
}))

export const YBEditor: FC<YBEditorProps> = ({ showToolbar = false, editorProps, singleLine, setVal }) => {

  const editor = useMemo(() => withHistory(withReact(createEditor())), [])

  const { renderElement, onKeyDown, renderLeaf } = useEditorPlugin(editor, {
    alertVariablesPlugin: true,
    basic: true,
    singleLine
  });

  const classes = useStyles();

  let Toolbar = useCallback(() => {
    if (!showToolbar) {
      return null;
    }
    return (
      <Grid className={classes.toolbarRoot}>
        <Grid item className={classes.formatIcons}>
          {
            Object.keys(ToolbarIcons).map((ic) => React.cloneElement(ToolbarIcons[ic].icon, {
              key: ic,
              onClick: (e: React.MouseEvent) => {
                e.preventDefault();
                toggleMark(editor, ic as TextDecorators);
              }
            }))
          }
        </Grid>
        <Grid item>

        </Grid>
      </Grid>
    )
  }, [showToolbar]);

  return (
    <div className={clsx(classes.root, { [classes.rootSingleLine]: singleLine })}>

      {Toolbar()}
      <Slate editor={editor} value={initialValue} {...editorProps} onChange={val => setVal(val)}>
        <Editable
          className={clsx(classes.editable, { [classes.singleLine]: singleLine })}
          renderElement={renderElement}
          onKeyDown={onKeyDown}
          renderLeaf={renderLeaf}
          spellCheck
          autoFocus
        />

      </Slate>
    </div>
  )
}
