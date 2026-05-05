import { makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';

import BulletIcon from '@app/redesign/assets/bullet.svg';

export const ListElementType = {
  NUMBERED: 'numbered',
  BULLET: 'bullet'
} as const;
export type ListElementType = (typeof ListElementType)[keyof typeof ListElementType];

interface ListElementBaseProps {
  listElementContent: React.ReactNode;

  contentClassName?: string;
}
type ListElementProps = ListElementBaseProps &
  (
    | { type: typeof ListElementType.BULLET }
    | { type: typeof ListElementType.NUMBERED; index: number }
  );

const useStyles = makeStyles((theme) => ({
  listElement: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  listElementContent: {
    color: theme.palette.grey[700],
    lineHeight: '16px'
  },
  listMarker: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    padding: theme.spacing(0.25),

    width: 24,
    height: 24
  },
  listElementIndex: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    width: 18,
    height: 18,
    padding: theme.spacing(1.25),

    borderRadius: '50%',
    backgroundColor: theme.palette.grey[100],

    fontSize: '10px',
    fontWeight: 500,
    lineHeight: '16px',
    color: theme.palette.grey[700]
  }
}));

export const ListElement = (props: ListElementProps) => {
  const classes = useStyles();

  return (
    <div className={classes.listElement}>
      {props.type === ListElementType.BULLET ? (
        <BulletIcon width={24} height={24} />
      ) : props.type === ListElementType.NUMBERED ? (
        <div className={classes.listMarker}>
          <div className={classes.listElementIndex}>{props.index}</div>
        </div>
      ) : null}
      <Typography
        className={clsx(classes.listElementContent, props.contentClassName)}
        variant="body2"
      >
        {props.listElementContent}
      </Typography>
    </div>
  );
};
