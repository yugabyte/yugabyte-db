import React, { FC } from "react";
import { Box, makeStyles, Typography } from "@material-ui/core";
import PostgresIcon from "@app/assets/postgres.svg";
import MySQLIcon from "@app/assets/mysql.svg";
import OracleIcon from "@app/assets/oracle.svg";

const useStyles = makeStyles(() => ({
  container: {
    display: 'flex',
    alignItems: 'center',
    gap: '4px',
    width: '100%',
    minWidth: 0
  },
  iconWrapper: {
    display: 'inline-flex',
    width: '24px',
    height: '24px',
    justifyContent: 'center',
    alignItems: 'center',
    flexShrink: 0,
    padding: 0,
    verticalAlign: 'middle'
  },
  icon: {
    width: '18.425px',
    height: '19px',
    flexShrink: 0,
    display: 'inline-block',
    verticalAlign: 'middle',
    position: 'relative',
    top: '1px'
  },
  text: {
    minWidth: 0,
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap',
    color: 'var(--Text-Color-Grey-900, #0B1117)',
    fontFamily: 'Inter',
    fontSize: '13px',
    fontStyle: 'normal',
    fontWeight: 400,
    lineHeight: '16px',
    display: 'flex',
    alignItems: 'center'
  }
}));

interface DatabaseTypeDisplayProps {
  type: string;
}

const formatDatabaseType = (type: string): string => {
  const typeMap: { [key: string]: string } = {
    'postgresql': 'PostgreSQL',
    'mysql': 'MySQL',
    'oracle': 'Oracle'
  };
  return typeMap[type.toLowerCase()] || type;
};

const getDatabaseIcon = (type: string) => {
  const lowerType = type.toLowerCase();
  if (lowerType.includes('postgresql')) return PostgresIcon;
  if (lowerType.includes('mysql')) return MySQLIcon;
  if (lowerType.includes('oracle')) return OracleIcon;
  return null;
};

export const DatabaseTypeDisplay: FC<DatabaseTypeDisplayProps> = ({ type }) => {
  const classes = useStyles();
  const Icon = getDatabaseIcon(type);
  const formattedType = formatDatabaseType(type);

  return (
    <Box className={classes.container} style={{alignItems: 'center'}}>
      {Icon && (
        <Box
          className={classes.iconWrapper}
          style={{display: 'flex',
          alignItems: 'center',
          justifyContent: 'center'}}
        >
          <Icon className={classes.icon} style={{display: 'block'}} />
        </Box>
      )}
      <Typography className={classes.text} style={{display: 'flex', alignItems: 'center'}}>
        {formattedType}
      </Typography>
    </Box>
  );
};
