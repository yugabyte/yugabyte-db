# ANTLR4 Go Query Result data parser generation rules for apache-age-go
Go driver for Apache AGE, graph extension for PostgreSQL.


### Build
#### 1) Generate query result data parser with ANTLR4
```
# prerequisites : 
#    - java over 8
#    - download ANTLR4 from https://www.antlr.org/download/antlr-4.9.2-complete.jar
#    - java -cp antlr-4.9.2-complete.jar org.antlr.v4.Tool  -Dlanguage=Go -visitor Age.g4 
```

