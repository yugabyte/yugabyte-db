/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/protocol/opcode.rs
 *
 *-------------------------------------------------------------------------
 */

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum OpCode {
    INVALID = 0,
    Reply = 1,
    Update = 2001,
    Insert = 2002,
    Reserved = 2003,
    Query = 2004,
    GetMore = 2005,
    Delete = 2006,
    KillCursors = 2007,
    Command = 2010,
    CommandReply = 2011,
    Compressed = 2012,
    Msg = 2013,
}

impl OpCode {
    pub fn from_value(code: i32) -> OpCode {
        match code {
            1 => OpCode::Reply,
            2001 => OpCode::Update,
            2002 => OpCode::Insert,
            2003 => OpCode::Reserved,
            2004 => OpCode::Query,
            2005 => OpCode::GetMore,
            2006 => OpCode::Delete,
            2007 => OpCode::KillCursors,
            2010 => OpCode::Command,
            2011 => OpCode::CommandReply,
            2012 => OpCode::Compressed,
            2013 => OpCode::Msg,
            _ => OpCode::INVALID,
        }
    }
}
