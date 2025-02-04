/*--------------------------------------------------------------------------------------------------
 *
 * yb_oid_assignment.c
 *        Functions for controlling assigning OIDs in xCluster target universes.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/backend/catalog/yb_catalog/yb_oid_assignment.c
 *
 *--------------------------------------------------------------------------------------------------
 */


#include "postgres.h"

#include "utils/builtins.h"
#include "utils/hsearch.h"
#include "utils/jsonfuncs.h"


static HTAB *yb_enum_label_assignment_map = NULL;
static bool yb_enum_label_assignment_exists = false;

static HTAB *yb_sequence_oid_assignment_map = NULL;
static bool yb_sequence_oid_assignment_exists = false;

/*
 * yb_enum_label_assignment_map key format is <oid>.<label>\0
 * Oid's are uint_32_t so take up at most 10 decimal digits.
 * <label>\0 is guaranteed to fit in NAMEDATALEN characters.
 */
#define YB_ENUM_LABEL_ASSIGNMENT_MAP_KEY_SIZE (10 + 1 + NAMEDATALEN)

typedef struct YbEnumLabelAssignmentMapEntry {
	/* encodes enum_oid, label */
	char key[YB_ENUM_LABEL_ASSIGNMENT_MAP_KEY_SIZE];
	Oid label_oid;
} YbEnumLabelAssignmentMapEntry;

static void
YbClearEnumLabelMap(void)
{
	HASHCTL ctl;

	if (yb_enum_label_assignment_map != NULL)
		hash_destroy(yb_enum_label_assignment_map);
	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize = YB_ENUM_LABEL_ASSIGNMENT_MAP_KEY_SIZE;
	ctl.entrysize = sizeof(YbEnumLabelAssignmentMapEntry);
	yb_enum_label_assignment_map = hash_create("YB enum label map",
											   /*initial size*/ 20, &ctl,
											   HASH_ELEM | HASH_STRINGS);
}

static void
YbCreateEnumLabelMapKey(Oid enum_oid, const char *label, char *key_buffer)
{
	/*
	 * For now ignore enum OID field.  See YbLookupOidAssignmentForEnumLabel
	 * for why.
	 */
	enum_oid = 42;

	int written_bytes = snprintf(key_buffer,
								 YB_ENUM_LABEL_ASSIGNMENT_MAP_KEY_SIZE, "%u.%s",
								 enum_oid, label);
	if (written_bytes >= YB_ENUM_LABEL_ASSIGNMENT_MAP_KEY_SIZE)
		elog(ERROR,
			 "unexpectedly large OID/label size in OID assignment (OID %u, "
			 "label '%s')",
			 enum_oid, label);
}

static void
YbInsertEnumLabel(Oid enum_oid, const char *label, Oid label_oid)
{
	char key[YB_ENUM_LABEL_ASSIGNMENT_MAP_KEY_SIZE];
	YbCreateEnumLabelMapKey(enum_oid, label, key);

	bool found;
	YbEnumLabelAssignmentMapEntry *entry =
		hash_search(yb_enum_label_assignment_map, key, HASH_ENTER, &found);
	if (!found)
		entry->label_oid = label_oid;
	else if (entry->label_oid != label_oid)
		elog(ERROR,
			 "attempt to provide multiple OIDs for enum label %u.%s: %u vs "
			 "%u",
			 enum_oid, label, entry->label_oid, label_oid);
}

/* Returns InvalidOid on not found. */
static Oid
YbLookupOidForEnumLabel(Oid enum_oid, const char *label)
{
	char key[YB_ENUM_LABEL_ASSIGNMENT_MAP_KEY_SIZE];
	YbCreateEnumLabelMapKey(enum_oid, label, key);
	bool found;
	YbEnumLabelAssignmentMapEntry *entry =
		hash_search(yb_enum_label_assignment_map, key, HASH_FIND, &found);
	if (found)
		return entry->label_oid;
	return InvalidOid;
}

/*
 * yb_sequence_oid_assignment_map key format is <schema>.<name>\0
 * <identifier>\0 is guaranteed to fit in NAMEDATALEN characters.
 */
#define YB_SEQUENCE_OID_ASSIGNMENT_MAP_KEY_SIZE (NAMEDATALEN + NAMEDATALEN)

typedef struct YbSequenceOidAssignmentMapEntry {
	/* encodes schema, name */
	char		key[YB_SEQUENCE_OID_ASSIGNMENT_MAP_KEY_SIZE];
	Oid			oid;
} YbSequenceOidAssignmentMapEntry;

static void
YbClearSequenceOidMap(void)
{
	HASHCTL ctl;

	if (yb_sequence_oid_assignment_map != NULL)
		hash_destroy(yb_sequence_oid_assignment_map);
	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize = YB_SEQUENCE_OID_ASSIGNMENT_MAP_KEY_SIZE;
	ctl.entrysize = sizeof(YbSequenceOidAssignmentMapEntry);
	yb_sequence_oid_assignment_map = hash_create("YB sequence OIDs map",
											   /*initial size*/ 20, &ctl,
											   HASH_ELEM | HASH_STRINGS);
}

static void
YbCreateSequenceOidMapKey(const char *schema, const char *name, char *key_buffer)
{
	int written_bytes = snprintf(key_buffer,
								 YB_SEQUENCE_OID_ASSIGNMENT_MAP_KEY_SIZE,
								 "%s.%s", schema, name);
	if (written_bytes >= YB_SEQUENCE_OID_ASSIGNMENT_MAP_KEY_SIZE)
		elog(ERROR,
			 "unexpectedly large schema/name in OID assignment (schema '%s', "
			 "name '%s')",
			 schema, name);
}

static void
YbInsertSequenceOid(const char *schema, const char *name, Oid sequence_oid)
{
	char key[YB_SEQUENCE_OID_ASSIGNMENT_MAP_KEY_SIZE];
	YbCreateSequenceOidMapKey(schema, name, key);

	bool found;
	YbSequenceOidAssignmentMapEntry *entry =
		hash_search(yb_sequence_oid_assignment_map, key, HASH_ENTER, &found);
	if (!found)
		entry->oid = sequence_oid;
	else if (entry->oid != sequence_oid)
		elog(ERROR,
			 "attempt to provide multiple OIDs for sequence %s.%s: %u vs "
			 "%u",
			 schema, name, entry->oid, sequence_oid);
}

/* Returns InvalidOid on not found. */
static Oid
YbLookupOidForSequence(const char *schema, const char *name)
{
	char key[YB_SEQUENCE_OID_ASSIGNMENT_MAP_KEY_SIZE];
	YbCreateSequenceOidMapKey(schema, name, key);

	bool found;
	YbSequenceOidAssignmentMapEntry *entry =
		hash_search(yb_sequence_oid_assignment_map, key, HASH_FIND, &found);
	if (found)
		return entry->oid;
	return InvalidOid;
}

/* Returns InvalidOid on bad input. */
static Oid
YbGetOidFromText(const text *input)
{
	if (!input)
		return InvalidOid;
	const char *cstring = text_to_cstring(input);
	char *end_ptr;
	Oid result = strtoul(cstring, &end_ptr, 10);
	if (result == 0 || result > UINT32_MAX || *end_ptr != '\0')
		return InvalidOid;
	return result;
}

PG_FUNCTION_INFO_V1(yb_xcluster_set_next_oid_assignments);

/*
 * New Yugabyte-specific Postgres function,
 * pg_catalog.yb_xcluster_set_next_oid_assignments.
 *
 * It is used by xCluster to control the assignment of OIDs for some objects
 * in the next DDL.  It is passed a JSON string with the assignment
 * information.
 *
 * Example:
 *    SELECT pg_catalog.yb_xcluster_set_next_oid_assignments(
 *       '{"enum_label_info":['                                        ||
 *            '{"label":"red","enum_oid":16405,"label_oid":16406},'    ||
 *            '{"label":"orange","enum_oid":16405,"label_oid":16408}'  ||
 *            ']}');
 *
 * This indicates that the label named red of the enum that has/will have OID
 * 16405 should be assigned the OID 16406.  Likewise, the same enum's orange
 * label should be assigned OID 16408.
 *
 * The enum_label_info key is optional; if it is present then all enum labels
 * created until the assignment is changed are expected to be covered by the
 * assignment.  In the example this means that if the DDL attempts to create a
 * label blue then an error will occur.  It is not an error if the DDL does
 * not create all the labels mentioned in the assignment.
 *
 *
 * Example:
 *    SELECT pg_catalog.yb_xcluster_set_next_oid_assignments(
 *       '{"sequence_info":['                                        ||
 *            '{"schema":"public","name":"my_sequence","oid":16406}' ||
 *            ']}');
 *
 * This indicates that the sequence named my_sequence in schema public should
 * be assigned the OID 16406.
 *
 * The sequence_info key is optional; if it is present then all sequences
 * created until the assignment is changed are expected to be covered by the
 * assignment.  In the example this means that if the DDL attempts to create a
 * sequence not_my_sequence then an error will occur.  It is not an error if
 * the DDL does not create all the sequences mentioned in the assignment.
 *
 *
 * You can remove the current assignment if any by using
 *
 *     SELECT pg_catalog.yb_xcluster_set_next_oid_assignments('{}');
 */

Datum
yb_xcluster_set_next_oid_assignments(PG_FUNCTION_ARGS)
{
	text *json_text = PG_GETARG_TEXT_P(0);

	YbClearEnumLabelMap();
	yb_enum_label_assignment_exists = false;
	text *enum_label_info = json_get_value(json_text, "enum_label_info");
	if (enum_label_info != NULL)
	{
		yb_enum_label_assignment_exists = true;
		int length = get_json_array_length(enum_label_info);
		for (int i = 0; i < length; i++)
		{
			text *label_info_entry = get_json_array_element(enum_label_info, i);
			char *label = text_to_cstring(json_get_denormalized_value(label_info_entry, "label"));
			text *label_oid_text = json_get_value(label_info_entry,
												  "label_oid");
			text *enum_oid_text = json_get_value(label_info_entry,
												 "enum_oid");

			Oid label_oid = YbGetOidFromText(label_oid_text);
			Oid enum_oid = YbGetOidFromText(enum_oid_text);
			if (label_oid == InvalidOid || enum_oid == InvalidOid)
			{
				elog(ERROR,
					 "corrupted JSON passed to "
					 "yb_xcluster_set_next_oid_assignments: '%s'",
					 text_to_cstring(json_text));
			}

			YbInsertEnumLabel(enum_oid, label, label_oid);
		}
	}

	YbClearSequenceOidMap();
	yb_sequence_oid_assignment_exists = false;
	text *sequence_info = json_get_value(json_text, "sequence_info");
	if (sequence_info != NULL)
	{
		yb_sequence_oid_assignment_exists = true;
		int length = get_json_array_length(sequence_info);
		for (int i = 0; i < length; i++)
		{
			text *sequence_info_entry = get_json_array_element(sequence_info, i);
			char *schema = text_to_cstring(json_get_denormalized_value(sequence_info_entry,
				"schema"));
			char *name = text_to_cstring(json_get_denormalized_value(sequence_info_entry,
				"name"));
			text *oid_text = json_get_value(sequence_info_entry, "oid");
			Oid sequence_oid = YbGetOidFromText(oid_text);
			if (sequence_oid == InvalidOid)
			{
				elog(ERROR,
					 "corrupted JSON passed to "
					 "yb_xcluster_set_next_oid_assignments: '%s'",
					 text_to_cstring(json_text));
			}

			YbInsertSequenceOid(schema, name, sequence_oid);
		}
	}

	PG_RETURN_VOID();
}

bool
YbUsingEnumLabelOidAssignment(void)
{
	return yb_enum_label_assignment_exists;
}

Oid
YbLookupOidAssignmentForEnumLabel(Oid enum_oid, const char *label)
{
	/*----------
	 * Currently we do not ensure that enums have the same *pg_type*
	 * OIDs.  We will fix that later, but in the meantime we take
	 * advantage of the fact that we currently never have a
	 * replicating DDL that refers to two different enums to ignore
	 * the actual enum OID field when comparing labels.
	 *
	 * See YbCreateEnumLabelMapKey for the temporary code to ignore the enum
	 * OID field.
	 *----------
	 */
	Oid label_oid = YbLookupOidForEnumLabel(enum_oid, label);
	if (label_oid == InvalidOid)
		elog(ERROR, "no OID assignment for enum label %u.%s in OID assignment",
			 enum_oid, label);
	return label_oid;
}

bool
YbUsingSequenceOidAssignment(void)
{
	return yb_sequence_oid_assignment_exists;
}

Oid
YbLookupOidAssignmentForSequence(const char *schema, const char *name)
{
	Oid sequence_oid = YbLookupOidForSequence(schema, name);
	if (sequence_oid == InvalidOid)
		elog(ERROR, "no OID assignment for sequence %s.%s in OID assignment",
			 schema, name);
	return sequence_oid;
}
