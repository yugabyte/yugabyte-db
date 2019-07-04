/****************************************************************************
 *  flashcache_ioctl.h
 *  FlashCache: Device mapper target for block-level disk caching
 *
 *  Copyright 2010 Facebook, Inc.
 *  Author: Mohan Srinivasan (mohan@facebook.com)
 *
 *  Based on DM-Cache:
 *   Copyright (C) International Business Machines Corp., 2006
 *   Author: Ming Zhao (mingzhao@ufl.edu)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#ifndef YB_ROCKSDB_THIRD_PARTY_FLASHCACHE_FLASHCACHE_IOCTL_H
#define YB_ROCKSDB_THIRD_PARTY_FLASHCACHE_FLASHCACHE_IOCTL_H
#ifdef __linux__

#include <linux/types.h>

#define FLASHCACHE_IOCTL 0xfe

enum {
  FLASHCACHEADDNCPID_CMD = 200,
  FLASHCACHEDELNCPID_CMD,
  FLASHCACHEDELNCALL_CMD,
  FLASHCACHEADDWHITELIST_CMD,
  FLASHCACHEDELWHITELIST_CMD,
  FLASHCACHEDELWHITELISTALL_CMD,
};

#define FLASHCACHEADDNCPID  _IOW(FLASHCACHE_IOCTL, FLASHCACHEADDNCPID_CMD, pid_t)
#define FLASHCACHEDELNCPID  _IOW(FLASHCACHE_IOCTL, FLASHCACHEDELNCPID_CMD, pid_t)
#define FLASHCACHEDELNCALL  _IOW(FLASHCACHE_IOCTL, FLASHCACHEDELNCALL_CMD, pid_t)

#define FLASHCACHEADDBLACKLIST    FLASHCACHEADDNCPID
#define FLASHCACHEDELBLACKLIST    FLASHCACHEDELNCPID
#define FLASHCACHEDELALLBLACKLIST FLASHCACHEDELNCALL

#define FLASHCACHEADDWHITELIST    _IOW(FLASHCACHE_IOCTL, FLASHCACHEADDWHITELIST_CMD, pid_t)
#define FLASHCACHEDELWHITELIST    _IOW(FLASHCACHE_IOCTL, FLASHCACHEDELWHITELIST_CMD, pid_t)
#define FLASHCACHEDELALLWHITELIST _IOW(FLASHCACHE_IOCTL, FLASHCACHEDELWHITELISTALL_CMD, pid_t)

#endif // __linux__
#endif // YB_ROCKSDB_THIRD_PARTY_FLASHCACHE_FLASHCACHE_IOCTL_H
