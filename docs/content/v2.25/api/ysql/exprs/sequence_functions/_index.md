---
title: Sequence functions [YSQL]
headerTitle: Sequence functions
linkTitle: Sequence functions
description: Functions operationg on sequences
menu:
  v2.25_api:
    identifier: sequence-functions
    parent: api-ysql-exprs
    weight: 50
type: indexpage
---

Sequences are special database objects designed to generate unique numeric identifiers. They are commonly used to create auto-incrementing primary keys for tables but can also serve other use cases requiring unique, sequential numbers.

A sequence is an independent object in the database that operates outside the scope of a table. It ensures thread-safe, concurrent access to increment or fetch the next value, making it highly reliable in multi-user environments.

Some of the key features of sequences are as follows

- Highly efficient for generating unique numbers.
- Customizable with options like starting value, increment, and cycling behavior.
- Supports multiple sequences in a single database.
- Works seamlessly with concurrent database operations.

YugabyteDB provides several functions to interact with sequences, allowing for precise control over their behavior and values:

1. [nextval](func_nextval/) - Fetches the next value in the sequence.
1. [currval](func_currval/) - Retrieves the current value of the sequence for the session.
1. [setval](func_setval/) - Sets the sequence to a specific value.
1. [lastval](func_lastval/) - Retrieves the most recent value fetched by nextval in the current session.

These functions enable seamless integration of sequences into your database logic, whether for automatic ID generation or custom sequence management.
