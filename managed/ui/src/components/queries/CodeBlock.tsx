import { useState } from 'react';
import copy from 'copy-to-clipboard';
import clsx from 'clsx';
import { makeStyles } from '@material-ui/core';

import { Highlighter } from '../../helpers/Highlighter';

interface CodeBlockProps {
  code: string;

  type?: string;
}

const useStyles = makeStyles((theme) => ({
  queryBlockContainer: {
    display: 'flex',
    gridGap: theme.spacing(1),

    padding: `${theme.spacing(1)}px ${theme.spacing(2)}px`,

    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    backgroundColor: theme.palette.ybacolors.backgroundGrayLight,
    borderRadius: '8px',
    '& pre': {
      flexGrow: '1',
      margin: '0px !important',
      padding: `${theme.spacing(1)}px !important`,
      border: 'none'
    }
  },
  copyButton: {
    display: 'flex',
    justifyContent: 'center',
    alignItems: 'center',

    marginLeft: 'auto',
    minWidth: '32px',
    height: '32px',

    borderRadius: '8px'
  },
  copyAvailable: {
    '&:hover': {
      cursor: 'pointer',
      backgroundColor: theme.palette.ybacolors.backgroundGrayDark
    }
  },
  copySuccess: {
    color: theme.palette.success[500],
    backgroundColor: theme.palette.primary[200],
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  }
}));
export const CodeBlock = ({ code, type = 'sql' }: CodeBlockProps) => {
  const [copied, setCopied] = useState<boolean>(false);
  const classes = useStyles();
  const handleCopy = () => {
    copy(code);
    setCopied(true);
    setTimeout(() => {
      setCopied(false);
    }, 2000);
  };

  return (
    <div className={classes.queryBlockContainer}>
      <Highlighter type={type} text={code} element="pre" />
      <div
        className={clsx(classes.copyButton, copied ? classes.copySuccess : classes.copyAvailable)}
        onClick={handleCopy}
      >
        {copied ? (
          <i className="fa fa-check" aria-hidden="true" />
        ) : (
          <i className="fa fa-clone" aria-hidden="true" />
        )}
      </div>
    </div>
  );
};
