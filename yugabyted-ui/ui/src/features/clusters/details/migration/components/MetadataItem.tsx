import React, { FC } from "react";
import { Box, makeStyles, Typography } from "@material-ui/core";

const useStyles = makeStyles((theme) => ({
  wrapper: {
    display: 'flex',
    alignItems: 'center',
    gap: 8, // 8px gap between label and value
    width: '100%',
  },
  vertical: {
    flexDirection: 'column',
    alignItems: 'flex-start',
    gap: "8px" // No gap between label and value in vertical
  },
  horizontal: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  label: {
    color: '#6D7C88',
    fontFamily: 'Inter, sans-serif',
    fontSize: '11.5px',
    fontWeight: 500,
    lineHeight: '16px',
    textTransform: 'uppercase',
    letterSpacing: 0,
    flexShrink: 1,
    whiteSpace: 'normal',
    wordBreak: 'break-word',
    maxWidth: '220px',
  },
  value: {
    color: '#0B1117',
    fontFamily: 'Inter, sans-serif',
    fontSize: '13px',
    fontWeight: 400,
    lineHeight: '16px',
    minWidth: 0,
    flexShrink: 1,
    wordBreak: 'break-word',
    whiteSpace: 'normal',
  },
  leftSection: {
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(2),
    flexShrink: 0,
    width: "100%",
    maxWidth: "400px"
  }
}));

interface MetadataItemProps {
  label: string | string[];
  value: React.ReactNode | React.ReactNode[];
  className?: string;
  layout?: 'vertical' | 'horizontal';
}

export const MetadataItem: FC<MetadataItemProps> = ({
  label,
  value,
  className,
  layout = 'vertical'
}) => {
  const classes = useStyles();

  if (Array.isArray(label) && Array.isArray(value) && label.length === value.length) {
    if (layout === 'vertical') {
      // Render each label above its value, one below the other
      return (
        <Box className={[classes.wrapper, classes.vertical, className].filter(Boolean).join(' ')}>
          {label.map((lbl, idx) => (
            <Box
              key={idx}
              display="flex"
              flexDirection="column"
              style={{ marginBottom: idx < label.length - 1 ? 16 : 0, gap: 4 }}
            >
              <Typography className={classes.label}>{lbl}</Typography>
              <Typography className={classes.value}>{(value as React.ReactNode[])[idx]}</Typography>
            </Box>
          ))}
        </Box>
      );
    } else {
      // Horizontal: table-like two columns, 8px gap between rows
      return (
        <Box
          className={[
            classes.wrapper,
            classes.horizontal,
            className
          ].filter(Boolean).join(' ')}
          style={{ rowGap: 8 }}
        >
          {/* Labels column */}
          <Box display="flex"
            flexDirection="column"
            alignItems="flex-start"
            style={{ rowGap: 8, width: 'fit-content', marginRight: 32 }}
          >
            {label.map((lbl, idx) => (
              <Typography className={classes.label} key={idx}>{lbl}</Typography>
            ))}
          </Box>
          {/* Values column */}
          <Box display="flex" flexDirection="column" alignItems="flex-start" style={{ rowGap: 8 }}>
            {(value as React.ReactNode[]).map((val, idx) => (
              <Typography className={classes.value} key={idx}>{val}</Typography>
            ))}
          </Box>
        </Box>
      );
    }
  }

  return (
    <Box
      className={[
        classes.wrapper,
        layout === 'vertical' ? classes.vertical : classes.horizontal,
        className
      ].filter(Boolean).join(' ')}
    >
      <Typography className={classes.label}>{label}</Typography>
      <Typography className={classes.value}>{value}</Typography>
    </Box>
  );
};
