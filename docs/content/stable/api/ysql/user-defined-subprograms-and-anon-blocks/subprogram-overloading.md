---
title: Subprogram overloading [YSQL]
headerTitle: Subprogram overloading
linkTitle: Subprogram overloading
description: Describes how subprogram overloading works [YSQL].
menu:
  stable:
    identifier: subprogram-overloading
    parent: user-defined-subprograms-and-anon-blocks
    weight: 40
type: docs
---

{{< tip title="Read the accounts of data type conversion and subprogram overloading in the PostgreSQL documentation." >}}
The possibility of overloading brings the risk of overall application behavior changes when a new overload of an existing subprogram's name is defined. You can see a few practical examples in the section [Choosing between overload candidates in the absence of a perfect match](#choosing-between-overload-candidates-in-the-absence-of-a-perfect-match) below.

The risk occurs because PostgreSQL, and therefore YSQL, support implicit data type conversion. The rules for data type conversion, and the consequences for how an overload candidate is selected, require careful explanation. The YSQL documentation doesn't reproduce the accounts. Rather, you should read the sections [Type Conversion](https://www.postgresql.org/docs/11/typeconv.html) and [Function Overloading](https://www.postgresql.org/docs/11/xfunc-overload.html) in the PostgreSQL documentation.
{{< /tip >}}

Subprograms with different _subprogram_call_signatures_ can share the same _[subprogram_name](../../syntax_resources/grammar_diagrams/#subprogram-name)_. If two or more subprograms share the same _subprogram_name_, then the _subprogram_name_ is said to be _overloaded_. Notice the relationship between the _subprogram_signature_ rule:

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#grammar" class="nav-link" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link active" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../syntax_resources/user-defined-subprograms-and-anon-blocks/subprogram_signature,arg_decl,arg_name,arg_mode,arg_type.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../syntax_resources/user-defined-subprograms-and-anon-blocks/subprogram_signature,arg_decl,arg_name,arg_mode,arg_type.diagram.md" %}}
  </div>
</div>

and the _subprogram_call_signature_ rule:

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#grammar-2" class="nav-link" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram-2" class="nav-link active" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar-2" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../syntax_resources/user-defined-subprograms-and-anon-blocks/subprogram_call_signature.grammar.md" %}}
  </div>
  <div id="diagram-2" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../syntax_resources/user-defined-subprograms-and-anon-blocks/subprogram_call_signature.diagram.md" %}}
  </div>
</div>

The _subprogram_signature_ is a list of _arg_decls_; and an _arg_decl_ has two optional components (_arg_type_ and _arg_mode_) and one mandatory component (_arg_type_). But the only significant part of the _subprogram_signature_ for distinguishing between overloads is the mandatory _arg_type_ component.<a name="subprogram-call-signature"></a>

{{< tip title="'OUT' arguments are not included in the 'subprogram_call_signature'." >}}
This rule is stated in the PostgreSQL documentation in the account of the  _[pg_proc](https://www.postgresql.org/docs/11/catalog-pg-proc.html)_ catalog table. See the description of the _proargtypes_ column.

Yugabyte recommends that you never create a function with an _out_ or _inout_ argument but, rather, that you return all values of interest by specifying an appropriate composite data type for the _returns_ clause. This implies preferring _returns table(...)_ over _returns setof_. The latter requires a list of _out_ arguments that correspond to the columns that you list, when you use the former, within the parenthesis of _table(...)_.

See, too, [this tip](../../the-sql-language/statements/ddl_create_function/#make-function-returns-mandatory) in the account of the _create function_ statement.

As it happens, PostgreSQL Version 11.2 (on which YSQL is based) does not yet allow a procedure to have an _out_ argument—so you must use an _inout_ argument instead. See [GitHub Issue #12348](https://github.com/yugabyte/yugabyte-db/issues/12348). YSQL, in some future version of YugabyteDB, will use a version of PostgreSQL where this limitation is removed. If you then decide to give a procedure a bare _out_ argument, then you should remember that it will not be included in the procedure's call signature.
{{< /tip >}}

## Negative examples

{{< note title="Notice that each example starts by dropping and re-creating the schema 's'." >}}

It's easy to get confused when you try _ad hoc_ tests, like those that this page shows, by the presence of previously-created overloads of the _same subprogram_name_ . All the examples shown on this page, therefore, create their objects in the schema _s1_ and drop and recreate this at the start.
{{< /note >}}

Attempt to create two functions that differ only by the _names_ of their single argument.


```plpgsql
drop schema if exists s1 cascade;
create schema s1;

create function s1.f(i in int)
  returns int
  language sql
as $body$
  select i*2;
$body$;

create function s1.f(j in int)
  returns int
  language sql
as $body$
  select j*2;
$body$;
```

The second _create function_ attempt causes the _42723_ error, _function "f" already exists with same argument types_. The names of the arguments, and their modes, are insignificant for distinguishing between overload candidates.

With the function _s1.f(i in int)_ still in place, now try this:

```plpgsql
create function s1.f(i in int)
  returns text
  language sql
as $body$
  select (i*2)::text;
$body$;
```

This, too, causes the _42723_ error. These two versions of _s1.f()_ have identical _subprogram_call_signatures_ and differ only in the data types that they return. This difference, too, is insignificant for distinguishing between overload candidates.

With the function _s1.f(i in int)_ still in place, now try this:

```plpgsql
create procedure s1.f(i in int)
  language sql
as $body$
begin
  raise info 'procedure f(i in int)';
end;
$body$;
```

Once again, you get the _42723_ error. The function and the procedure have the same _subprogram_call_signature_—but the kind of the subprogram, _function_ or _procedure_ is also insignificant for distinguishing between overload candidates.

The only significant information for distinguishing between overload candidates is the _subprogram_call_signature_.

## Simple positive, but contrived, example

Try this:

```plpgsql
drop schema if exists s1 cascade;
create schema s1;

create function s1.f(i in text)
  returns text
  language plpgsql
as $body$
begin
  return 'text overload';
end
$body$;

drop function if exists f(varchar) cascade;
create function s1.f(i in varchar)
  returns text
  language plpgsql
as $body$
begin
  return 'varchar overload';
end
$body$;

select
  (select s1.f('dog'::text))     as "typecast to 'text'",
  (select s1.f('dog'::varchar))  as "typecast to 'varchar'";
```

This is the result:

```output
 typecast to 'text' | typecast to 'varchar'
--------------------+-----------------------
 text overload      | varchar overload
```

Now leave out the explicit typecast:

```plpgsql
select s1.f('dog');
```

This is the result:

```output
       f
---------------
 text overload
```

Now drop the _text_ overload and repeat the identical query:

```plpgsql
drop function s1.f(text);
select s1.f('dog');
```

This is the new result:

```output
        f
------------------
 varchar overload
```

These two query results emphasize how treacherous the terrain is. The literal _'dog'_ is legal both as a _text_ value and as a _varchar_ value; and nothing about it indicates which is preferred. A committee decided, doubtless after careful thought, that:

- The interpretation of the unadorned literal _'dog' as a _text_ value is preferred over the interpretation as a _varchar_ value—when both candidates exist.
- The interpretation of the unadorned literal _'dog' as a _varchar_ value allowed when no better candidate exists.

Notice that, now, even this attempt, where and explicit typecast to _text_ is used, still resolves to the _varchar_ candidate without error.

```plpgsql
select s1.f('dog'::text);
```

Repeat this test but add a _char_ overload into the mix. Experiment with various orders of dropping until you get down to just a single remaining overload. (This implies six different experiments.) The results might confuse you—and you'd probably find it hard to summarize them succinctly.

Most ordinary programmers find it impossible to memorise the actual rules that govern scenarios like these and to rely on them safely. The next section pursues this point with some more examples.

{{< tip title="Don't mix the use of _text_ and _varchar_ in the same application." >}}

Yugabyte recommends that you preemptively avoid the risk of overload confusion like the examples in this subsection show by making an up-front decision, at design specification time, between using only _text_ or only _varchar_ for string values. Use _text_ when you have no interest in constraining the lengths of the strings; and use _varchar_ when you do want to constrain specific kinds of strings to specific lengths.

(The _char_ data type has its own peculiarities. It's best to avoid this data type altogether unless you can write down a very convincing reason in the design specification for why it's needed.)
{{< /tip >}}

## Choosing between overload candidates in the absence of a perfect match

The following examples hammer home the point that the rules for choosing which overload to use when there is more than one plausible candidate, and which implicit typecasts are supported, are tortuous.

First create the basic setup with a _text_ overload, an _int_ overload, and a _boolean_ overload of the name _s1.f_:

```plpgsql
drop schema if exists s1 cascade;
create schema s1;

create function s1.f(i in text)
  returns text
  language plpgsql
as $body$
begin
  return 'text overload';
end
$body$;

drop function if exists s1.f(int) cascade;
create function s1.f(i in int)
  returns text
  language plpgsql
as $body$
begin
  return 'int overload';
end
$body$;

drop function if exists s1.f(boolean) cascade;
create function s1.f(i in boolean)
  returns text
  language plpgsql
as $body$
begin
  return 'boolean overload';
end
$body$;

select
  (select s1.f('dog'))  as "s1.f('dog')",
  (select s1.f(17))     as "s1.f(17)",
  (select s1.f(true))   as "s1.f(true))";
```

This is the result:

```output
  s1.f('dog')  |   s1.f(17)   |   s1.f(true))
---------------+--------------+------------------
 text overload | int overload | boolean overload
```

Now do this:

```plpgsql
drop function s1.f(text);

select
  (select s1.f('42'))  as "s1.f('42')",
  (select s1.f(17))    as "s1.f(17)";
```

The _drop_ leaves behind the _int_ and the _boolean_ overloads. But, because the string _'true'_ can be typecast to _boolean_, the _select_ causes this error:

```output
42725: function s1.f(unknown) is not unique
```

Remove the source of confusion and try the query again:

```plpgsql
drop function s1.f(boolean);

select
  (select s1.f('42'))  as "s1.f('42')",
  (select s1.f(17))    as "s1.f(17)";
```

Now the error goes away and you get this result:

```output
  s1.f('42')  |   s1.f(17)
--------------+--------------
 int overload | int overload
```

Now remove the _int_ overload and put back the _boolean_ overload and try a different query:

```plpgsql
drop function s1.f(int);
create function s1.f(i in boolean)
  returns text
  language plpgsql
as $body$
begin
  return 'boolean overload';
end
$body$;

select
  (select s1.f(true))    as "s1.f(true)",
  (select s1.f('true'))  as "s1.f('true')";
```

Again, there's no error; and you get this result:

```output
    s1.f(true)    |   s1.f('true')
------------------+------------------
 boolean overload | boolean overload
```

Finally, put back the _text_ overload and repeat the same query:

```plpgsql
create function s1.f(i in text)
  returns text
  language plpgsql
as $body$
begin
  return 'text overload';
end
$body$;

select
  (select s1.f(true))    as "s1.f(true)",
  (select s1.f('true'))  as "s1.f('true')";
```

This time, the fact that _'true'_ can be implicitly converted to a _boolean_ does not cause the _"function s1.f(unknown) is not unique"_ error. Rather, this is the result:

```outout
    s1.f(true)    | s1.f('true')
------------------+---------------
 boolean overload | text overload
```

{{< tip title="Consider distinguishing between intended overloads by using different names." >}}

The tests shown on this page make the point that the rules for choosing which overload to use in the presence of more than one plausible candidate are tortuous. And in such scenarios, YSQL sometimes chooses a candidate without error and sometimes you get the _"function s1.f(unknown) is not unique"_ error.

Consider the built-in functions that operate on (plain) _json_ and on _jsonb_ values. Several of them could have been implemented as overloads of the same name. But even so, the designers of this functionality decided to use a naming convention where what might have been an overload pair have _json_ or _jsonb_ in their names.

This approach is always open to you.
{{< /tip >}}
