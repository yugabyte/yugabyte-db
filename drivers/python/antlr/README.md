# ANTLR4 Python3 Agtype parser generation rules for apache-age 
Python driver for Apache AGE, graph extension for PostgreSQL.


### Build
#### 1) Generate Agtype parser with ANTLR4
```
# prerequisites : 
#    - java over 8
#    - download ANTLR4 from https://www.antlr.org/download/antlr-4.9.2-complete.jar
#    - java -cp antlr-4.9.2-complete.jar org.antlr.v4.Tool  -Dlanguage=Python3 -visitor -o ../age/gen ../../Agtype.g4 
```

