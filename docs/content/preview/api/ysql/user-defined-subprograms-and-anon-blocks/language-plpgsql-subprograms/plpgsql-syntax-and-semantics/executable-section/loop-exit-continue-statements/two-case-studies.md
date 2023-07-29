---
title: >
  Two case studies: Using various kinds of "loop" statement, the "exit" statement, and the "continue" statement [YSQL]
headerTitle: >
  Two case studies: Using various kinds of "loop" statement, the "exit" statement, and the "continue" statement
linkTitle: Two case studies
description: Two case studies to demonstrate the use of the "for" loop (over a range of integers), the "for" loop (over the results of a query), the "foreach" loop, the "infinite" loop, the "exit" statement, and the "continue" statement [YSQL].
menu:
  preview:
    identifier: two-case-studies
    parent: loop-exit-continue-statements
    weight: 60
type: docs
showRightNav: true
---

The first case study demonstrates the use of these statements:

- the _foreach loop_ statement to iterate over all the elements of an array
- the _infinite loop_ statement together with the _exit_ statement to extract words from lines of text
- the _cursor for loop_ statement to iterate over the results of a table function
- the _integer range for loop_ statement to iterate over the elements of an array that lie between a specified range of index values

The second case study demonstrates the use of these statements:

- the _foreach loop_ statement to iterate over all the elements of an array
- the _integer range for loop_ statement to iterate over the characters in a string
- the _exit_ statement to terminate the iteration of both a _foreach loop_ statement and an _integer range for loop_ statement when enough results have been found
- the _continue_ statement to abandon the current iteration of a loop and to start the next iteration of the same loop when a test shows that the current result fails to satisfy a requirement
- the _continue_ statement with a _label_ to abandon the current iteration of an _inner_ loop and to start the next iteration of the _outer_ loop when a test shows that the _inner_ loop has already met the processing requirement for the _outer_ loop's current element.

## Common setup

Both studies use an array of lines of text. Set it up thus:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create function s.dickens_lines()
  returns text[]
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  lines constant text[] not null := array[
    ' The carrier’s horse was the laziest horse in the world, I should hope, and shuffled along, ',
    ' with his head down, as if he liked to keep people waiting to whom the packages were        ',
    ' directed. I fancied, indeed, that he sometimes chuckled audibly over this reflection,      ',
    ' but the carrier said he was only troubled with a cough. The carrier had a way of keeping   ',
    ' his head down, like his horse, and of drooping sleepily forward as he drove, with one of   ',
    ' his arms on each of his knees. I say “drove”, but it struck me that the cart would have    ',
    ' gone to Yarmouth quite as well without him, for the horse did all that; and as to          ',
    ' conversation, he had no idea of it but whistling. Peggotty had a basket of refreshments    ',
    ' on her knee, which would have lasted us out handsomely, if we had been going to London by  ',
    ' the same conveyance. We ate a good deal, and slept a good deal. Peggotty always went to    ',
    ' sleep with her chin upon the handle of the basket, her hold of which never relaxed; and I  ',
    ' could not have believed unless I had heard her do it, that one defenceless woman could     ',
    ' have snored so much. We made so many deviations up and down lanes, and were such a long    ',
    ' time delivering a bedstead at a public house, and calling at other places, that I was      ',
    ' quite tired, and very glad, when we saw Yarmouth. It looked rather spongy and soppy, I     ',
    ' thought, as I carried my eye over the great dull waste that lay across the river; and I    ',
    ' could not help wondering, if the world were really as round as my geography book said, how ',
    ' any part of it came to be so flat. But I reflected that Yarmouth might be situated at one  ',
    ' of the poles; which would account for it. As we drew a little nearer, and saw the whole    ',
    ' adjacent prospect lying a straight low line under the sky, I hinted to Peggotty that a     ',
    ' mound or so might have improved it; and also that if the land had been a little more       ',
    ' separated from the sea, and the town and the tide had not been quite so much mixed up,     ',
    ' like toast and water, it would have been nicer. But Peggotty said, with greater emphasis   ',
    ' than usual, that we must take things as we found them, and that, for her part, she was     ',
    ' proud to call herself a Yarmouth Bloater. When we got into the street (which was strange   ',
    ' enough to me) and smelt the fish, and pitch, and oakum, and tar, and saw the sailors       ',
    ' walking about, and the carts jingling up and down over the stones, I felt that I had done  ',
    ' so busy a place an injustice; and said as much to Peggotty, who heard my expressions of    ',
    ' delight with great complacency, and told me it was well known (I suppose to those who had  ',
    ' the good fortune to be born Bloaters) that Yarmouth was, upon the whole, the finest place  ',
    ' in the universe. He was waiting for us, in fact, at the public house; and asked me how I   ',
    ' found myself, like an old acquaintance. I did not feel, at first, that I knew him as well  ',
    ' as he knew me, because he had never come to our house since the night I was born, and      ',
    ' naturally he had the advantage of me. But our intimacy was much advanced by his taking me  ',
    ' on his back to carry me home. He was, now, a huge, strong fellow of six feet high, broad   ',
    ' in proportion, and round-shouldered; but with a simpering boy’s face and curly light hair  ',
    ' that gave him quite a sheepish look. He was dressed in a canvas jacket, and a pair of such ',
    ' very stiff trousers that they would have stood quite as well alone, without any legs in    ',
    ' them. And you couldn’t so properly have said he wore a hat, as that he was covered in a    ',
    ' top, like an old building, with something pitchy. Ham carrying me on his back and a small  '
    ];
begin
  return lines;
end;
$body$;
```

It isn't necessary to clutter the text literal with _$$_ quoting because the text itself contains no straight single quotes but, rather, uses a proper (curly) apostrophe where this is needed.

## Case study #1: extract the words from an array of text lines

### Problem statement

The heart of the solution is the code that extracts the words from a single line of text. This relies on just the _infinite loop_ statement together with the _exit when_ statement. But extending the problem statement to require extracting the words from an array of text lines brings opportunities to demonstrate other kinds of _loop_ statement.


### The code

First create a function to find the first word in a line of text and to return that word together with what remains when it is removed from the text.

```plpgsql
create type s.first_word_and_remainder as (word text, remainder text);

create function s.split_string(str in text)
  returns s.first_word_and_remainder
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  result s.first_word_and_remainder;
begin
  result.word      := (regexp_match(str, '[a-zA-Z’]+'))[1];
  result.remainder := regexp_replace(str, result.word, '');
  return result;
end;
$body$;
```

The design of this function isn't of interest for the present case study's pedagogy. But it's of note that the implementation takes just two lines. Because the words in the text used here happen to use only latin letters and curly apostrophe, the regular expression that defines a word is very short. It would need to be more elaborate it had to accommodate European languages that use letters like these: _Å Ø å æ ø_. Test it like this.

```plpgsql
create function s.split_string_test_results(str_in in text)
  returns text
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  str  text not null := str_in;
  r    text not null := '';

  split_string s.first_word_and_remainder;
begin
  loop
    split_string := s.split_string(str);
    exit when split_string.word is null;
    r := r||split_string.word||' | ';
    str := split_string.remainder;
  end loop;
  return r;
end;
$body$;

select s.split_string_test_results(' Mary’s most frequent typo: confusing “its” and “it’s”. ');
```

This is the result:

```output
 Mary’s | most | frequent | typo | confusing | its | and | it’s | 
```

The test demonstrates the main algorithm for this case study. It uses the _infinite loop_ statement like this:

```output
set up for the first iteration
repeat
  do some processing
  exit when finished
  note this iteration's result
  set up for the next iteration
end repeat
```

It's a common pattern—and it's sometimes called the _"loop-and-a-half"_ in generic computer science writings.

As an exercise, try to rewrite it using a _while loop_ statement. You'll find that the code is more elaborate—and is therefore harder to understand and more prone to bugs.

The solution to this case study's actual problem is just a trivial extension to the function _s.split_string_test_results()_. First, create a helper function so that the interesting code can be separated from the display of the results. It shows just the first ten words and the last ten words that have been extracted. Each set of ten words is split over two lines.

```plpgsql
create function s.display(results in text[])
  returns table(x text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  lb int not null := 0;
begin
  for j in 1..4 loop
    case j
      when 1 then lb := 1;
      when 2 then lb := 6;
      when 3 then lb := cardinality(results) - 9; x := ''; return next;
      when 4 then lb := cardinality(results) - 4;
    end case;

    x := '';
    for j in lb..(lb + 4) loop
      x := x||rpad(results[j], 15);
    end loop;
    return next;
  end loop;
end;
$body$;
```

It turns out the using the function _s.tokenized_lines()_ demonstrates another interesting use of a _loop_ statement. Look for this:

```plpgsql
for z in (select x from s.display(results)) loop
  return next;
end loop;
```

This is the standard pattern for consuming the output of a table function whose purpose is to display some results within a second table function whose purpose is to display other results.

Create the function _s.tokenized_lines()_ to implement the case study's solution:

```plpgsql
create function s.tokenized_lines()
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  lines           constant text[] not null := s.dickens_lines();
  line                     text   not null := '';
  results                  text[] not null := '{}';

  split_line             s.first_word_and_remainder;
begin
  <<lines>>
  foreach line in array lines loop
    <<words>>
    loop
      split_line := s.split_string(line);
      exit words when split_line.word is null;
      results := results||split_line.word;
      line := split_line.remainder;
    end loop words;
  end loop lines;

  for z in (select x from s.display(results)) loop
    return next;
  end loop;
end;
$body$;
```

Execute it:

```plpgsql
select s.tokenized_lines();
```

This is the result:

```output
 The            carrier’s      horse          was            the            
 laziest        horse          in             the            world          
 
 pitchy         Ham            carrying       me             on             
 his            back           and            a              small 
```

## Case study #2: compose a string of a specified number of vowels from each text line in an array until a specified number of such vowel strings have been composed 

This case study doesn't use any kinds of _loop_ statement that case study #1 doesn't use. But it does use the _continue_ statement. Especially, it shows its use with a _label_ to abort the current iteration of an _inner_ loop and then to start the next iteration of an _outer_ loop. You might like to try to rewrite the code without using the _continue_ statement. You'll need to use an _if_ statement or a _case_ statement together with a dedicated guard variable. The result will be cluttered code that is also less efficient than the code that's shown here.

### Problem statement

Suppose that you have a one-dimensional _text_ array where each successive element holds the next line from, say, a novel. The goal is to implement this:

- Make the first line current.
- Traverse the characters in the current line from the first one to (up to) the last one.
- Inspect the current character. If it's a vowel, then note it. Otherwise skip it. 
- Abandon the current line:
  - _either_ if it contains _Peggotty_
  - _or_ when the same vowel is noted twice in succession
  - _or_ when the specified number of distinct vowels (say _seven_) have been noted
- Record the vowel string only if its length is the specified number of distinct vowels.
- Make the next line current. Repeat until the specified number of vowel strings (say _ten_) of the specified length have been recorded.
- Display the recorded vowel strings.
- Finish.

Never mind if the goal seems artificial. It's always instructive to solve a well-specified problem when doing so requires some creative thinking, design, implementation, and testing.

### The code

First, create a trivial helper function to test if a single character is a Latin alphabet vowel:


```plpgsql
create function s.vowel(c in text)
  returns boolean
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  assert length(c) = 1;
  return c in ('a', 'e', 'i', 'o', 'u');
end;
$body$;
```

Now create the table function _s.f()_ to find the vowel strings and display them.

```plpgsql
create function s.vowels_from_lines(no_of_vowels in int, no_of_results in int)
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  lines           constant text[] not null := s.dickens_lines();
  line                     text   not null := '';
  prev                     text   not null := '';
  c                        text   not null := '';
  vowels                   text   not null := '';
  results                  text[] not null := '{}';
  r                        int    not null := 0;
begin
  <<lines>>
  foreach line in array lines loop           -- Inspect up to every line.
    continue lines when
      line like '%Peggotty%';                -- Don't process any "line" that
                                             -- contains "Peggotty".
    c      := '';
    vowels := '';
    prev   := '';

    <<characters>>
    for pos in 1..length(line) loop          -- Inspect up to every character in each line.
      c := substring(line from pos for 1);
      if s.vowel(c)
        then vowels := vowels||c;
        continue lines when
          c = prev;                          -- Abandon this line when two successively
                                             -- found vowels are the same.
        prev := c;
      end if;

      exit characters when
        length(vowels) >= no_of_vowels;      -- Don't want "vowels" values longer
                                             -- than "no_of_vowels".
    end loop characters;

    if length(vowels) = no_of_vowels then
      r := r + 1;
      results[r] := vowels;
    end if;

    exit lines when
      r >= no_of_results;                    -- Finish when have enough results.
  end loop lines;

  foreach z in array results loop
    return next;
  end loop;
end;
$body$;
```

Test it like this:

```plpgsql
select s.vowels_from_lines(7, 10);
```

This is the result:

```output
 eaieoea
 ueaieai
 ieaoiei
 iaoeaoi
 aeoeoue
 uieieae
 ouoeoei
 oeoeiou
 eouoeae
 aiaouae
```

Notice these uses of the _continue_ statement:

```plpgsql
-- At top level in the outer "lines" loop.
continue lines when line like '%Peggotty%';
```

and:

```plpgsql
-- Nested inside the inner "characters" loop within the outer "lines" loop.
continue lines when c = prev;
```

to abandon processing the current line and to move to the next one in the outer _lines_ loop.

Notice these uses of the _exit_ statement:

```plpgsql
exit characters when length(vowels) >= no_of_vowels;
```

to exit the inner _characters_ loop when the vowels string is long enough. And:

```plpgsql
exit lines when r >= no_of_results;
```

to exit the _lines_ loop when enough results have been recorded.
