// Copyright (c) YugaByte, Inc.
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
// NOLINT(build/header_guard)

YB_STATUS_CODE(Ok, OK, 0, "OK")
YB_STATUS_CODE(NotFound, NOT_FOUND, 1, "Not found")
YB_STATUS_CODE(Corruption, CORRUPTION, 2, "Corruption")
YB_STATUS_CODE(NotSupported, NOT_SUPPORTED, 3, "Not implemented")
YB_STATUS_CODE(InvalidArgument, INVALID_ARGUMENT, 4, "Invalid argument")
YB_STATUS_CODE(IOError, IO_ERROR, 5, "IO error")
YB_STATUS_CODE(AlreadyPresent, ALREADY_PRESENT, 6, "Already present")
YB_STATUS_CODE(RuntimeError, RUNTIME_ERROR, 7, "Runtime error")
YB_STATUS_CODE(NetworkError, NETWORK_ERROR, 8, "Network error")
YB_STATUS_CODE(IllegalState, ILLEGAL_STATE, 9, "Illegal state")
YB_STATUS_CODE(NotAuthorized, NOT_AUTHORIZED, 10, "Not authorized")
YB_STATUS_CODE(Aborted, ABORTED, 11, "Aborted")
YB_STATUS_CODE(RemoteError, REMOTE_ERROR, 12, "Remote error")
YB_STATUS_CODE(ServiceUnavailable, SERVICE_UNAVAILABLE, 13, "Service unavailable")
YB_STATUS_CODE(TimedOut, TIMED_OUT, 14, "Timed out")
YB_STATUS_CODE(Uninitialized, UNINITIALIZED, 15, "Uninitialized")
YB_STATUS_CODE(ConfigurationError, CONFIGURATION_ERROR, 16, "Configuration error")
YB_STATUS_CODE(Incomplete, INCOMPLETE, 17, "Incomplete")
YB_STATUS_CODE(EndOfFile, END_OF_FILE, 18, "End of file")
YB_STATUS_CODE(InvalidCommand, INVALID_COMMAND, 19, "Invalid command")
YB_STATUS_CODE(QLError, QL_ERROR, 20, "Query error")
YB_STATUS_CODE(InternalError, INTERNAL_ERROR, 21, "Internal error")
YB_STATUS_CODE(Expired, EXPIRED, 22, "Operation expired")
YB_STATUS_CODE(LeaderNotReadyToServe, LEADER_NOT_READY_TO_SERVE, 23,
               "Leader not ready to serve requests")
YB_STATUS_CODE(LeaderHasNoLease, LEADER_HAS_NO_LEASE, 24, "Leader does not have a valid lease")
YB_STATUS_CODE(TryAgain, TRY_AGAIN_CODE, 25, "Operation failed. Try again")
YB_STATUS_CODE(Busy, BUSY, 26, "Resource busy")
YB_STATUS_CODE(ShutdownInProgress, SHUTDOWN_IN_PROGRESS, 27, "Shutdown in progress")
YB_STATUS_CODE(MergeInProgress, MERGE_IN_PROGRESS, 28, "Merge in progress")
YB_STATUS_CODE(Combined, COMBINED_ERROR, 29,
               "Combined status representing multiple status failures")
YB_STATUS_CODE(SnapshotTooOld, SNAPSHOT_TOO_OLD, 30, "Snapshot too old")
YB_STATUS_CODE(CacheMissError, CACHE_MISS_ERROR, 32, "Cache miss error")
YB_STATUS_CODE(TabletSplit, TABLET_SPLIT, 33, "Tablet split has occured")
YB_STATUS_CODE(ReplicationSlotLimitReached, REPLICATION_SLOT_LIMIT_REACHED, 34,
               "Replication slot limit reached")
