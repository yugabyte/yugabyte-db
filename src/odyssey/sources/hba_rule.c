
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <machinarium.h>
#include <kiwi.h>
#include <odyssey.h>

od_hba_rule_name_item_t *od_hba_rule_name_item_add(od_hba_rule_name_t *name)
{
	od_hba_rule_name_item_t *item;
	item = (od_hba_rule_name_item_t *)malloc(sizeof(*item));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(*item));
	od_list_init(&item->link);
	od_list_append(&name->values, &item->link);
	return item;
}

od_hba_rule_t *od_hba_rule_create()
{
	od_hba_rule_t *hba;
	hba = (od_hba_rule_t *)malloc(sizeof(*hba));
	if (hba == NULL)
		return NULL;
	memset(hba, 0, sizeof(*hba));
	od_list_init(&hba->database.values);
	od_list_init(&hba->user.values);
	return hba;
}

void od_hba_rule_free(od_hba_rule_t *hba)
{
	od_list_t *i, *n;
	od_hba_rule_name_item_t *item;
	od_list_foreach_safe(&hba->database.values, i, n)
	{
		item = od_container_of(i, od_hba_rule_name_item_t, link);
		free(item->value);
		free(item);
	}
	od_list_foreach_safe(&hba->user.values, i, n)
	{
		item = od_container_of(i, od_hba_rule_name_item_t, link);
		free(item->value);
		free(item);
	}
	free(hba);
}

void od_hba_rules_init(od_hba_rules_t *rules)
{
	od_list_init(rules);
}

void od_hba_rules_free(od_hba_rules_t *rules)
{
	od_list_t *i, *n;
	od_list_foreach_safe(rules, i, n)
	{
		od_hba_rule_t *hba;
		hba = od_container_of(i, od_hba_rule_t, link);
		od_hba_rule_free(hba);
	}
}

void od_hba_rules_add(od_hba_rules_t *rules, od_hba_rule_t *rule)
{
	od_list_init(&rule->link);
	od_list_append(rules, &rule->link);
}
