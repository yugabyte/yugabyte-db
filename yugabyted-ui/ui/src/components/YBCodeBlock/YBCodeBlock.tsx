import React, { FC, useRef } from 'react';
import clsx from 'clsx';
import { Box, IconButton, makeStyles } from '@material-ui/core';
import '@app/assets/fonts/Menlo-Regular.woff';
import { YBButton } from '../YBButton/YBButton';
import { AlertVariant } from '..';
import { useToast, copyToClipboard } from '@app/helpers';
import { useTranslation } from 'react-i18next';
import CopyIcon from '@app/assets/copy.svg';

export interface CodeBlockProps {
  highlightSyntax?: boolean;
  showCopyButton?: boolean;
  showCopyIconButton?: boolean;
  multiBlock?: boolean;
  blockClassName?: string;
  codeClassName?: string;
  preClassName?: string;
  lineClassName?: string;
  showLineNumbers?: boolean;
  text: string | string[] | React.ReactElement | React.ReactElement[];
}

const useStyles = makeStyles((theme) => ({
  block: {
    display: 'flex',
    borderRadius: theme.shape.borderRadius,
    background: theme.palette.info[400],
    margin: theme.spacing(0, 0, 1, 0),
    border: `1px solid ${theme.palette.grey[300]}`,
    position: 'relative',
  },
  lineNo: {
    color: theme.palette.grey[600],
    background: theme.palette.grey[200],
    padding: theme.spacing(1),
    lineHeight: 2.7,
    height: '100%',
  },
  code: {
    display: 'block',
    flex: '1',
    padding: theme.spacing(1),
    fontFamily: 'Menlo-Regular, Courier, monospace',
    fontSize: 13,
    lineHeight: '17px',
    color: theme.palette.grey[800],
    borderRadius: theme.spacing(0.5),
    height: '100%',
    overflowX: 'hidden',
    '&:hover': {
      overflowX: 'auto'
    }
  },
  copyButton: {
    position: 'absolute',
    right: 10,
    top: 10,
    backgroundColor: theme.palette.info[200],
    '& .MuiButton-label': {
      textTransform: 'uppercase',
      fontSize: 11.5
    }
  },
  copyIcon: {
    position: 'absolute',
    right: 0,
    top: 0,
    color: theme.palette.primary[600],
    padding: theme.spacing(1)
  },
  inlineButton: {
    top: theme.spacing(0.5),
    right: theme.spacing(0.75),
    display: 'none'
  },
  hoverBlock: {
    padding: theme.spacing(0.5, 0.75),

    '&:hover': {
      background: theme.palette.primary[200]
    },

    '&:hover .yb-copy-btn': {
      display: 'block'
    }
  },
  innerBlock: {
    fontFamily: 'Menlo-Regular, Courier, monospace',
    whiteSpace: 'pre-wrap',
    margin: 0,
    lineHeight: 1.7,
  },
  hashLine: {
    color: '#13A868',
  },
  tagWord: {
    color: '#2B59C3'
  }
}));

export const YBCodeBlock: FC<CodeBlockProps> = ({
  highlightSyntax = false,
  showCopyButton,
  showCopyIconButton,
  multiBlock,
  text,
  blockClassName,
  codeClassName,
  preClassName,
  lineClassName,
  showLineNumbers,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const ref = useRef<HTMLPreElement>(null);

  const { addToast } = useToast();

  const copy = async (value: string) =>
    copyToClipboard(value, {
      onSuccess: () => addToast(AlertVariant.Success, t('common.copyCodeSuccess'), 3000),
      onError: (msg?: string) =>
        addToast(AlertVariant.Error, msg || 'Failed to copy to clipboard', 5000)
    });

  const copyBlock = () => {
    let textToCopy = ref?.current?.innerText ?? '';

    // Fallback to original text prop if ref doesn't contain text
    if (textToCopy.trim() === '' && typeof text === 'string') {
      textToCopy = text;
    }

    if (textToCopy.trim() === '') {
      addToast(AlertVariant.Error, 'No text found to copy', 5000);
      return;
    }
    copy(textToCopy);
  };
  const copyLineBlock = (ev: React.MouseEvent) => copy((ev?.currentTarget?.previousSibling as HTMLElement).innerText);
  const specialChars: string[] = [' ','\t',',',';','.',':','!','?','(',')','[',']','{','}','"',"'"];
  const parseText = (text: string) => {
    return text.split('\n').map((line, lineIndex) => {
      const trimmedLine = line.trim();
      const isHashLine = trimmedLine.startsWith('#');
      const formattedWords = [];
      let currentWord = '';
      let tagStart = -1;

      for (let i = 0; i < line.length; i++) {
        const char = line[i];

        if (specialChars.includes(char) === true) {
          if (currentWord) {
            formattedWords.push(currentWord);
            currentWord = '';
            tagStart = -1;
          }
          formattedWords.push(char);
        } else {
          if (char === '<' && currentWord === '') {
            tagStart = i;
          }
          currentWord += char;
          if (char === '>' && tagStart !== -1) {
            formattedWords.push(
              <span key={`word-${lineIndex}-${formattedWords.length}`} className={classes.tagWord}>
                {currentWord}
              </span>
            );
            currentWord = '';
            tagStart = -1;
          }
        }
      }

      if (currentWord) {
        formattedWords.push(currentWord);
      }

      const lineContent = isHashLine ? (
        <span className={classes.hashLine}>{formattedWords}</span>
      ) : (
        formattedWords
      );

      return (
        <span key={`${lineIndex}`} className={clsx({ [classes.hashLine]: isHashLine })}>
          {lineContent}
          <br />
        </span>
      );
    });
  };
  return (
    <>
      <Box className={clsx(classes.block, blockClassName)}>
        {showLineNumbers &&
          <Box className={clsx(classes.lineNo, lineClassName)}>
            {typeof text === "string" && text.split('\n').map((_, index) => <>{index + 1}<br /></>)}
          </Box>
        }
        {multiBlock && Array.isArray(text) ? (
          text.map((val: string | React.ReactElement, index: number) => (
            <code className={clsx(classes.code, classes.hoverBlock)} key={`code-block-line-${String(index + 1)}`}>
              <pre className={classes.innerBlock}>
                {(highlightSyntax === true && typeof val === "string") ? parseText(val) : val}
              </pre>
              {showCopyButton && (
                <YBButton
                  variant="ghost"
                  className={clsx(classes.copyButton, classes.inlineButton, 'yb-copy-btn')}
                  onClick={copyLineBlock}
                >
                  {t('common.copy')}
                </YBButton>
              )}
            </code>
          ))
        ) : (
          <code className={clsx(classes.code, codeClassName)}>
            <pre className={clsx(classes.innerBlock, preClassName)} ref={ref}>
              {(typeof text === 'string' && highlightSyntax === true) ? parseText(text) : text}
            </pre>
            {showCopyButton && (
              <YBButton variant="ghost" className={classes.copyButton} onClick={copyBlock}>
                {t('common.copy')}
              </YBButton>
            )}
            {showCopyIconButton && (
              <IconButton className={classes.copyIcon} onClick={copyBlock}>
                <CopyIcon fontSize="small" />
              </IconButton>
            )}
          </code>
        )}
      </Box>
    </>
  );
};
