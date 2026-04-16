Development Notes
===============================================================================

This folders contains weird ideas, failed tests and dodgy dead ends.

We use [jupyter] to write these notebooks. Most of them are probably outdated.

[jupyter]: https://jupyter.org/

Here's how you can install [jupyter]:

```bash
$ pip3 install --upgrade pip
$ pip3 install --r docs/dev/requirements
$ export PATH=$PATH:~/.local/bin
```

And then launch [jupyter]:

```bash
$ jupyter notebook
# or
$ jupyter notebook --no-browser --port 9999
```

Or convert the notebooks

```bash
jupyter nbconvert docs/dev/*.ipynb --to markdown
```
