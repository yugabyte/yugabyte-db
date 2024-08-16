
HELIO_CORE_INC_DIR = $(OSS_SRC_DIR)/pg_helio_core/include
HELIO_API_INC_DIR = $(OSS_SRC_DIR)/pg_helio_api/include
HELIO_DISTRIBUTED_INC_DIR = $(OSS_SRC_DIR)/internal/pg_helio_distributed/include

OSS_COMMON_SQL_HEADER = $(wildcard $(OSS_SRC_DIR)/common_header.sql)

PG_HELIO_CORE_EXT_DIR=$(OSS_SRC_DIR)/pg_helio_core/
PG_HELIO_CORE_EXT_NAME=pg_helio_core

PG_HELIO_API_EXT_DIR=$(OSS_SRC_DIR)/pg_helio_api/
PG_HELIO_API_EXT_NAME=pg_helio_api

PG_HELIO_DISTRIBUTED_EXT_DIR=$(OSS_SRC_DIR)/internal/pg_helio_distributed/
PG_HELIO_DISTRIBUTED_EXT_NAME=pg_helio_distributed

include $(OSS_SRC_DIR)/Makefile.cflags
