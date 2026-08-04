import copy from 'copy-to-clipboard';
import { YBButton } from '../YBButton/YBButton';
import { useEffect, useState } from 'react';
import { Typography, makeStyles } from '@material-ui/core';

interface YBCopyButtonProps {
  text: string;
  btnText: string;
  className?: any;
  options?: YBCopyButtonOptions;
}

interface YBCopyButtonOptions {
  debug: boolean;
  message: string;
}

const useStyles = makeStyles((theme) => ({
  copyButton: {
    padding: '0px',
    height: '0px'
  },
  buttonText: {
    color: '#2B59C3',
    fontSize: '11.5px',
    fontFamily: 'Inter',
    fontWeight: 400,
    textDecoration: 'underline'
  }
}));

export const YBCopyButton = ({ text, options, btnText, className }: YBCopyButtonProps) => {
  const helperClasses = useStyles();
  const [clicked, setClicked] = useState<boolean>(false);
  const [caption, setCaption] = useState<string>(btnText);

  const onCopyButtonClicked = () => {
    copy(text, options);
    setClicked(true);
    setTimeout(() => {
      setClicked(false);
    }, 2500);
  };

  useEffect(() => {
    setCaption(clicked ? 'Copied' : btnText);
  }, [clicked]);

  return (
    <YBButton variant={'pill'} onClick={onCopyButtonClicked} className={helperClasses.copyButton}>
      <span className={helperClasses.buttonText}>{caption}</span>
    </YBButton>
  );
};
