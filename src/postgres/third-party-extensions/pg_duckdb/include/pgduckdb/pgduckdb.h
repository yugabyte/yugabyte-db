#pragma once

/* Values for the backslash_quote GUC */
typedef enum {
	MOTHERDUCK_OFF,
	MOTHERDUCK_ON,
	MOTHERDUCK_AUTO,
} MotherDuckEnabled;

// pgduckdb.cpp
extern "C" void _PG_init(void);

// pgduckdb_hooks.c
void DuckdbInitHooks(void);
