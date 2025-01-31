/* src/interfaces/ecpg/preproc/variable.c */

#include "postgres_fe.h"

#include "preproc_extern.h"

static struct variable *allvariables = NULL;

struct variable *
new_variable(const char *name, struct ECPGtype *type, int brace_level)
{
	struct variable *p = (struct variable *) mm_alloc(sizeof(struct variable));

	p->name = mm_strdup(name);
	p->type = type;
	p->brace_level = brace_level;

	p->next = allvariables;
	allvariables = p;

	return p;
}

static struct variable *
find_struct_member(char *name, char *str, struct ECPGstruct_member *members, int brace_level)
{
	char	   *next = strpbrk(++str, ".-["),
			   *end,
				c = '\0';

	if (next != NULL)
	{
		c = *next;
		*next = '\0';
	}

	for (; members; members = members->next)
	{
		if (strcmp(members->name, str) == 0)
		{
			if (next == NULL)
			{
				/* found the end */
				switch (members->type->type)
				{
					case ECPGt_array:
						return new_variable(name, ECPGmake_array_type(ECPGmake_simple_type(members->type->u.element->type, members->type->u.element->size, members->type->u.element->counter), members->type->size), brace_level);
					case ECPGt_struct:
					case ECPGt_union:
						return new_variable(name, ECPGmake_struct_type(members->type->u.members, members->type->type, members->type->type_name, members->type->struct_sizeof), brace_level);
					default:
						return new_variable(name, ECPGmake_simple_type(members->type->type, members->type->size, members->type->counter), brace_level);
				}
			}
			else
			{
				*next = c;
				if (c == '[')
				{
					int			count;

					/*
					 * We don't care about what's inside the array braces so
					 * just eat up the character
					 */
					for (count = 1, end = next + 1; count; end++)
					{
						switch (*end)
						{
							case '[':
								count++;
								break;
							case ']':
								count--;
								break;
							default:
								break;
						}
					}
				}
				else
					end = next;

				switch (*end)
				{
					case '\0':	/* found the end, but this time it has to be
								 * an array element */
						if (members->type->type != ECPGt_array)
							mmfatal(PARSE_ERROR, "incorrectly formed variable \"%s\"", name);

						switch (members->type->u.element->type)
						{
							case ECPGt_array:
								return new_variable(name, ECPGmake_array_type(ECPGmake_simple_type(members->type->u.element->u.element->type, members->type->u.element->u.element->size, members->type->u.element->u.element->counter), members->type->u.element->size), brace_level);
							case ECPGt_struct:
							case ECPGt_union:
								return new_variable(name, ECPGmake_struct_type(members->type->u.element->u.members, members->type->u.element->type, members->type->u.element->type_name, members->type->u.element->struct_sizeof), brace_level);
							default:
								return new_variable(name, ECPGmake_simple_type(members->type->u.element->type, members->type->u.element->size, members->type->u.element->counter), brace_level);
						}
						break;
					case '-':
						if (members->type->type == ECPGt_array)
							return find_struct_member(name, ++end, members->type->u.element->u.members, brace_level);
						else
							return find_struct_member(name, ++end, members->type->u.members, brace_level);
						break;
						break;
					case '.':
						if (members->type->type == ECPGt_array)
							return find_struct_member(name, end, members->type->u.element->u.members, brace_level);
						else
							return find_struct_member(name, end, members->type->u.members, brace_level);
						break;
					default:
						mmfatal(PARSE_ERROR, "incorrectly formed variable \"%s\"", name);
						break;
				}
			}
		}
	}

	return NULL;
}

static struct variable *
find_struct(char *name, char *next, char *end)
{
	struct variable *p;
	char		c = *next;

	/* first get the mother structure entry */
	*next = '\0';
	p = find_variable(name);

	if (c == '-')
	{
		if (p->type->type != ECPGt_array)
			mmfatal(PARSE_ERROR, "variable \"%s\" is not a pointer", name);

		if (p->type->u.element->type != ECPGt_struct && p->type->u.element->type != ECPGt_union)
			mmfatal(PARSE_ERROR, "variable \"%s\" is not a pointer to a structure or a union", name);

		/* restore the name, we will need it later */
		*next = c;

		return find_struct_member(name, ++end, p->type->u.element->u.members, p->brace_level);
	}
	else
	{
		if (next == end)
		{
			if (p->type->type != ECPGt_struct && p->type->type != ECPGt_union)
				mmfatal(PARSE_ERROR, "variable \"%s\" is neither a structure nor a union", name);

			/* restore the name, we will need it later */
			*next = c;

			return find_struct_member(name, end, p->type->u.members, p->brace_level);
		}
		else
		{
			if (p->type->type != ECPGt_array)
				mmfatal(PARSE_ERROR, "variable \"%s\" is not an array", name);

			if (p->type->u.element->type != ECPGt_struct && p->type->u.element->type != ECPGt_union)
				mmfatal(PARSE_ERROR, "variable \"%s\" is not a pointer to a structure or a union", name);

			/* restore the name, we will need it later */
			*next = c;

			return find_struct_member(name, end, p->type->u.element->u.members, p->brace_level);
		}
	}
}

static struct variable *
find_simple(char *name)
{
	struct variable *p;

	for (p = allvariables; p; p = p->next)
	{
		if (strcmp(p->name, name) == 0)
			return p;
	}

	return NULL;
}

/* Note that this function will end the program in case of an unknown */
/* variable */
struct variable *
find_variable(char *name)
{
	char	   *next,
			   *end;
	struct variable *p;
	int			count;

	next = strpbrk(name, ".[-");
	if (next)
	{
		if (*next == '[')
		{
			/*
			 * We don't care about what's inside the array braces so just eat
			 * up the characters
			 */
			for (count = 1, end = next + 1; count; end++)
			{
				switch (*end)
				{
					case '[':
						count++;
						break;
					case ']':
						count--;
						break;
					default:
						break;
				}
			}
			if (*end == '.')
				p = find_struct(name, next, end);
			else
			{
				char		c = *next;

				*next = '\0';
				p = find_simple(name);
				if (p == NULL)
					mmfatal(PARSE_ERROR, "variable \"%s\" is not declared", name);

				*next = c;
				switch (p->type->u.element->type)
				{
					case ECPGt_array:
						return new_variable(name, ECPGmake_array_type(ECPGmake_simple_type(p->type->u.element->u.element->type, p->type->u.element->u.element->size, p->type->u.element->u.element->counter), p->type->u.element->size), p->brace_level);
					case ECPGt_struct:
					case ECPGt_union:
						return new_variable(name, ECPGmake_struct_type(p->type->u.element->u.members, p->type->u.element->type, p->type->u.element->type_name, p->type->u.element->struct_sizeof), p->brace_level);
					default:
						return new_variable(name, ECPGmake_simple_type(p->type->u.element->type, p->type->u.element->size, p->type->u.element->counter), p->brace_level);
				}
			}
		}
		else
			p = find_struct(name, next, next);
	}
	else
		p = find_simple(name);

	if (p == NULL)
		mmfatal(PARSE_ERROR, "variable \"%s\" is not declared", name);

	return p;
}

void
remove_typedefs(int brace_level)
{
	struct typedefs *p,
			   *prev;

	for (p = prev = types; p;)
	{
		if (p->brace_level >= brace_level)
		{
			/* remove it */
			if (p == types)
				prev = types = p->next;
			else
				prev->next = p->next;

			if (p->type->type_enum == ECPGt_struct || p->type->type_enum == ECPGt_union)
				free(p->struct_member_list);
			free(p->type);
			free(p->name);
			free(p);
			if (prev == types)
				p = types;
			else
				p = prev ? prev->next : NULL;
		}
		else
		{
			prev = p;
			p = prev->next;
		}
	}
}

void
remove_variables(int brace_level)
{
	struct variable *p,
			   *prev;

	for (p = prev = allvariables; p;)
	{
		if (p->brace_level >= brace_level)
		{
			/* is it still referenced by a cursor? */
			struct cursor *ptr;

			for (ptr = cur; ptr != NULL; ptr = ptr->next)
			{
				struct arguments *varptr,
						   *prevvar;

				for (varptr = prevvar = ptr->argsinsert; varptr != NULL; varptr = varptr->next)
				{
					if (p == varptr->variable)
					{
						/* remove from list */
						if (varptr == ptr->argsinsert)
							ptr->argsinsert = varptr->next;
						else
							prevvar->next = varptr->next;
					}
				}
				for (varptr = prevvar = ptr->argsresult; varptr != NULL; varptr = varptr->next)
				{
					if (p == varptr->variable)
					{
						/* remove from list */
						if (varptr == ptr->argsresult)
							ptr->argsresult = varptr->next;
						else
							prevvar->next = varptr->next;
					}
				}
			}

			/* remove it */
			if (p == allvariables)
				prev = allvariables = p->next;
			else
				prev->next = p->next;

			ECPGfree_type(p->type);
			free(p->name);
			free(p);
			if (prev == allvariables)
				p = allvariables;
			else
				p = prev ? prev->next : NULL;
		}
		else
		{
			prev = p;
			p = prev->next;
		}
	}
}


/*
 * Here are the variables that need to be handled on every request.
 * These are of two kinds: input and output.
 * I will make two lists for them.
 */

struct arguments *argsinsert = NULL;
struct arguments *argsresult = NULL;

void
reset_variables(void)
{
	argsinsert = NULL;
	argsresult = NULL;
}

/* Insert a new variable into our request list.
 * Note: The list is dumped from the end,
 * so we have to add new entries at the beginning */
void
add_variable_to_head(struct arguments **list, struct variable *var, struct variable *ind)
{
	struct arguments *p = (struct arguments *) mm_alloc(sizeof(struct arguments));

	p->variable = var;
	p->indicator = ind;
	p->next = *list;
	*list = p;
}

/* Append a new variable to our request list. */
void
add_variable_to_tail(struct arguments **list, struct variable *var, struct variable *ind)
{
	struct arguments *p,
			   *new = (struct arguments *) mm_alloc(sizeof(struct arguments));

	for (p = *list; p && p->next; p = p->next);

	new->variable = var;
	new->indicator = ind;
	new->next = NULL;

	if (p)
		p->next = new;
	else
		*list = new;
}

void
remove_variable_from_list(struct arguments **list, struct variable *var)
{
	struct arguments *p,
			   *prev = NULL;
	bool		found = false;

	for (p = *list; p; p = p->next)
	{
		if (p->variable == var)
		{
			found = true;
			break;
		}
		prev = p;
	}
	if (found)
	{
		if (prev)
			prev->next = p->next;
		else
			*list = p->next;
	}
}

/* Dump out a list of all the variable on this list.
   This is a recursive function that works from the end of the list and
   deletes the list as we go on.
 */
void
dump_variables(struct arguments *list, int mode)
{
	char	   *str_zero;

	if (list == NULL)
		return;

	str_zero = mm_strdup("0");

	/*
	 * The list is build up from the beginning so lets first dump the end of
	 * the list:
	 */

	dump_variables(list->next, mode);

	/* Then the current element and its indicator */
	ECPGdump_a_type(base_yyout, list->variable->name, list->variable->type, list->variable->brace_level,
					list->indicator->name, list->indicator->type, list->indicator->brace_level,
					NULL, NULL, str_zero, NULL, NULL);

	/* Then release the list element. */
	if (mode != 0)
		free(list);

	free(str_zero);
}

void
check_indicator(struct ECPGtype *var)
{
	/* make sure this is a valid indicator variable */
	switch (var->type)
	{
			struct ECPGstruct_member *p;

		case ECPGt_short:
		case ECPGt_int:
		case ECPGt_long:
		case ECPGt_long_long:
		case ECPGt_unsigned_short:
		case ECPGt_unsigned_int:
		case ECPGt_unsigned_long:
		case ECPGt_unsigned_long_long:
			break;

		case ECPGt_struct:
		case ECPGt_union:
			for (p = var->u.members; p; p = p->next)
				check_indicator(p->type);
			break;

		case ECPGt_array:
			check_indicator(var->u.element);
			break;
		default:
			mmerror(PARSE_ERROR, ET_ERROR, "indicator variable must have an integer type");
			break;
	}
}

struct typedefs *
get_typedef(char *name)
{
	struct typedefs *this;

	for (this = types; this && strcmp(this->name, name) != 0; this = this->next);
	if (!this)
		mmfatal(PARSE_ERROR, "unrecognized data type name \"%s\"", name);

	return this;
}

void
adjust_array(enum ECPGttype type_enum, char **dimension, char **length, char *type_dimension, char *type_index, int pointer_len, bool type_definition)
{
	if (atoi(type_index) >= 0)
	{
		if (atoi(*length) >= 0)
			mmfatal(PARSE_ERROR, "multidimensional arrays are not supported");

		*length = type_index;
	}

	if (atoi(type_dimension) >= 0)
	{
		if (atoi(*dimension) >= 0 && atoi(*length) >= 0)
			mmfatal(PARSE_ERROR, "multidimensional arrays are not supported");

		if (atoi(*dimension) >= 0)
			*length = *dimension;

		*dimension = type_dimension;
	}

	if (pointer_len > 2)
		mmfatal(PARSE_ERROR, ngettext("multilevel pointers (more than 2 levels) are not supported; found %d level",
									  "multilevel pointers (more than 2 levels) are not supported; found %d levels", pointer_len),
				pointer_len);

	if (pointer_len > 1 && type_enum != ECPGt_char && type_enum != ECPGt_unsigned_char && type_enum != ECPGt_string)
		mmfatal(PARSE_ERROR, "pointer to pointer is not supported for this data type");

	if (pointer_len > 1 && (atoi(*length) >= 0 || atoi(*dimension) >= 0))
		mmfatal(PARSE_ERROR, "multidimensional arrays are not supported");

	if (atoi(*length) >= 0 && atoi(*dimension) >= 0 && pointer_len)
		mmfatal(PARSE_ERROR, "multidimensional arrays are not supported");

	switch (type_enum)
	{
		case ECPGt_struct:
		case ECPGt_union:
			/* pointer has to get dimension 0 */
			if (pointer_len)
			{
				*length = *dimension;
				*dimension = mm_strdup("0");
			}

			if (atoi(*length) >= 0)
				mmfatal(PARSE_ERROR, "multidimensional arrays for structures are not supported");

			break;
		case ECPGt_varchar:
		case ECPGt_bytea:
			/* pointer has to get dimension 0 */
			if (pointer_len)
				*dimension = mm_strdup("0");

			/* one index is the string length */
			if (atoi(*length) < 0)
			{
				*length = *dimension;
				*dimension = mm_strdup("-1");
			}

			break;
		case ECPGt_char:
		case ECPGt_unsigned_char:
		case ECPGt_string:
			/* char ** */
			if (pointer_len == 2)
			{
				*length = *dimension = mm_strdup("0");
				break;
			}

			/* pointer has to get length 0 */
			if (pointer_len == 1)
				*length = mm_strdup("0");

			/* one index is the string length */
			if (atoi(*length) < 0)
			{
				/*
				 * make sure we return length = -1 for arrays without given
				 * bounds
				 */
				if (atoi(*dimension) < 0 && !type_definition)

					/*
					 * do not change this for typedefs since it will be
					 * changed later on when the variable is defined
					 */
					*length = mm_strdup("1");
				else if (strcmp(*dimension, "0") == 0)
					*length = mm_strdup("-1");
				else
					*length = *dimension;

				*dimension = mm_strdup("-1");
			}
			break;
		default:
			/* a pointer has dimension = 0 */
			if (pointer_len)
			{
				*length = *dimension;
				*dimension = mm_strdup("0");
			}

			if (atoi(*length) >= 0)
				mmfatal(PARSE_ERROR, "multidimensional arrays for simple data types are not supported");

			break;
	}
}
