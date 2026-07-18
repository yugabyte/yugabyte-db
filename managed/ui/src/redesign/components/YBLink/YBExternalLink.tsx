import { makeStyles } from '@material-ui/core';
import { ReactNode } from 'react';

import ExternalLinkIcon from '../../assets/external-link.svg';

interface YBExternalLinkProps {
  href: string;
  children?: ReactNode;
}

const useStyles = makeStyles((theme) => ({
  externalLink: {
    alignItems: 'center'
  },
  externalLinkIcon: {
    verticalAlign: 'bottom',
    marginLeft: theme.spacing(0.5)
  }
}));

export const YBExternalLink = ({ href, children }: YBExternalLinkProps) => {
  const classes = useStyles();

  return (
    <a href={href} target="_blank" rel="noopener noreferrer" className={classes.externalLink}>
      {children}
      <ExternalLinkIcon className={classes.externalLinkIcon} />
    </a>
  );
};
