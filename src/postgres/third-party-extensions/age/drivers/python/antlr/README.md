# ANTLR4 Python3 Agtype parser generation rules for apache-age 
Python driver for Apache AGE, graph extension for PostgreSQL.


### Build
#### 1) Generate Agtype parser with ANTLR4
```
# prerequisites : 
#    - java over 8
#    - download ANTLR4 from https://www.antlr.org/download/antlr-4.11.1-complete.jar
#    - java -cp antlr-4.11.1-complete.jar org.antlr.v4.Tool  -Dlanguage=Python3 -visitor -o ../age/gen ../../Agtype.g4 
```


#### 2) Remove the *.interp & *.tokens files

#### 3) Proceed to unit testing
```
python -m unittest -v test_age_py.py
```
```
python -m unittest -v test_agtypes.py
`
