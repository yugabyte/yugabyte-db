//    pg_dump_anon.go
//    A basic wrapper to export anonymized data with pg_dump and psql
//

package main

import (
  "flag"
  "fmt"
  "io"
  "log"
  "os"
  "os/exec"
  "path/filepath"
  "regexp"
  "strings"
  "context"
  "database/sql"
  "golang.org/x/crypto/ssh/terminal"
  "github.com/lib/pq"
)

//
// Constants
//

// Errors
const error_pq_ssl_not_enabled = "pq: SSL is not enabled on the server"

// Queries

// arg1 is snapshotname
// arg2 is the filters
// arg3 is the table name
// arg4 is the tablesample (if any)
const sql_copy_to = `
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SET TRANSACTION SNAPSHOT '%s';
COPY (SELECT %s FROM %s %s) TO STDOUT WITH CSV;
ROLLBACK;
`

// arg1 is the table name
const sql_copy_from = `
COPY %s FROM STDIN WITH CSV;
`

const sql_export_snapshot = `
SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT pg_export_snapshot();
`
const sql_mask_filters = `
SELECT anon.mask_filters('%s'::REGCLASS);
`

const sql_mask_schema  = `
SELECT pg_catalog.current_setting('anon.maskschema');
`

// can't use string_agg here because cnd.Run want an array of options
const sql_sequences = `
SELECT
  '--table='||quote_ident(sequence_schema)||'.'||quote_ident(sequence_name)
FROM information_schema.sequences
WHERE sequence_schema != 'anon';
`
// arg1 is the table name
const sql_tablesample =`
SELECT anon.get_tablesample_ratio('%s'::REGCLASS::OID);
`

const sql_anon_version = `
SELECT anon.version()
`

//
// Global variables
//

// pq connection string
var dsn []string = []string{ "application_name=pg_dump_anon" }

// export options
var pg_dump_opts []string= []string{}

// client options
var psql_opts []string = []string {
  "--quiet",
  "--tuples-only",
  "--no-align",
  "--no-psqlrc",
}

// output file pointer
var output *os.File = os.Stdout       // by default, use standard ouput

// snapshot identifier
var snapshotname string

// Command line flags
var f_dbname string
var f_data_only string
var f_encoding string
var f_exclude_schema string
var f_exclude_table string
var f_exclude_table_data string
var f_file string
var f_format string
var f_host string
var f_no_password bool
var f_password bool
var f_port string
var f_schema string
var f_table string
var f_username string
var f_verbose bool

// filter_out_extension remove an extension from the DDL stream
// There's no clean way to exclude an extension from a dump
// This is a pragmatic approach
func filter_out_extension(ddl_lines string, extension string) string {

  exclude_lines := []string {
    fmt.Sprintf("-- Name: %s;.*\n", extension),
    fmt.Sprintf("CREATE EXTENSION IF NOT EXISTS %s .*\n", extension),
    fmt.Sprintf("-- Name: EXTENSION %s;.*\n", extension),
    fmt.Sprintf("COMMENT ON EXTENSION %s .*\n", extension)}

  for i := range exclude_lines {
    re := regexp.MustCompile(exclude_lines[i])
    ddl_lines=re.ReplaceAllString(ddl_lines, "")
  }
  return ddl_lines
}

func pg_dump_to_array(options []string) []byte {
  cmd := exec.Command("pg_dump",options...)  // #nosec G204
  verbose(cmd)
  output, err := cmd.Output()
  if err != nil {
    verbose(output)
    log.Fatal(err)
  }
  return output
}

// pg_dump is a simple wrapper to the pg_dump command
// The result is sent directly to the standard output
func pg_dump(options []string, output *os.File) {
  cmd := exec.Command("pg_dump",options...)  // #nosec G204
  verbose(cmd)
  cmd.Stdout = os.Stdout
  cmd.Stderr = os.Stderr
  err := cmd.Run()
  if err != nil {
    log.Fatal(err.Error())
  }
}

// append_option adds a parameter to pg_dump and psql wrappers
//
// option_value is optional
func append_option(option_flag string, option_value ...string) {
  if len(option_value) > 0 {
    if option_value[0] != "" {
      append_psql_option(option_flag,option_value[0])
      append_pg_dump_option(option_flag,option_value[0])
    }
  } else {
    append_psql_option(option_flag)
    append_pg_dump_option(option_flag)
  }
}

// append_psql_option adds a parameter to the psql wrappers
//
// option_value is optional
func append_psql_option(option_flag string, option_value ...string) {
  if len(option_value) > 0 && option_value[0] != "" {
    if option_value[0] != "" {
      psql_opts = append(psql_opts,option_flag+option_value[0])
    }
  } else {
    psql_opts = append(psql_opts,option_flag)
  }
}

// append_option adds a parameter to the pg_dump psql wrappers
//
// option_value is optional
func append_pg_dump_option(option_flag string, option_value ...string) {
  if len(option_value) > 0 {
    if option_value[0] != "" {
      pg_dump_opts = append(pg_dump_opts,option_flag+option_value[0])
    }
  } else {
    pg_dump_opts = append(pg_dump_opts,option_flag)
  }
}

// append_dsn adds a parameter for the lib/pq connection
func append_dsn(option_flag string, option_value string) {
  if option_value != "" {
     dsn = append(dsn, fmt.Sprintf("%s=%s",option_flag,option_value))
  }
}

// redirectOutput opens a file and redirects all stdout writes into it
func redirectOutput(logfile string) func() {
  // open file read/write | create if not exist | clear file at open if exists
  f, err := os.OpenFile(
                filepath.Clean(logfile),
                os.O_RDWR|os.O_CREATE|os.O_TRUNC,
                0600)
  if err !=nil {
    log.Fatal(err)
  }
  mw := io.MultiWriter(f)
  // get pipe reader and writer
  // writes to pipe writer come out pipe reader
  r, w, _ := os.Pipe()
  // replace stdout with pipe writer
  // all writes to stdout will go through pipe instead (fmt.print, log)
  os.Stdout = w

  // create channel to control exit
  // will block until all copies are finished
  exit := make(chan bool)


  go func() {
    // copy all reads from pipe to multiwriter, which writes to stdout and file
    _,_ = io.Copy(mw, r)
    // when r or w is closed copy will finish and true will be sent to channel
    exit <- true
  }()

  // function to be deferred in main until program exits
  return func() {
    // close writer then block on exit channel
    // this will let mw finish writing before the program exits
    _ = w.Close()
    <-exit
    // close file after all writes have finished
    _ = f.Close()
  }
}

func verbose(message ...any) {
  if f_verbose {
    log.Println("pg_dump_anon: ",message)
  }
}

func main() {

//##############################################################################
// Basic checks
//##############################################################################
  _ , err := exec.LookPath("psql")
  if err != nil {
    log.Fatal("Can't find psql, check your PATH")
  }

  _ , err = exec.LookPath("pg_dump")
  if err != nil {
    log.Fatal("Can't find pg_dump, check your PATH")
  }

//##############################################################################
// 0. Parsing the command line arguments
//##############################################################################


// pg_dump_anon supports a subset of pg_dump options
//
// some arguments will be pushed to `pg_dump` and/or `psql` while others need
// specific treatment ( especially the `--file` option)
//


  flag.StringVar(&f_data_only,"a","","dump only the data, not the schema")
  flag.StringVar(&f_data_only,"data-only", "", "dump only the data, not the schema")
  flag.StringVar(&f_dbname,"d","","database to dump")
  flag.StringVar(&f_dbname,"dbname", "", "database to dump")
  flag.StringVar(&f_encoding,"E","","dump the data in encoding ENCODING")
  flag.StringVar(&f_encoding,"encoding","","dump the data in encoding ENCODING")
  flag.StringVar(&f_exclude_table,"T","","Exclude the specified table(s)")
  flag.StringVar(&f_exclude_table,"exclude-table","","Exclude the specified schema(s)")
  flag.StringVar(&f_exclude_table_data,"exclude-table-data","","do NOT dump data for the specified table(s)")
  flag.StringVar(&f_file,"f", "", "Output file")
  flag.StringVar(&f_file,"file", "", "Output file")
  flag.StringVar(&f_format,"F", "", "output file format (only plain is supported)")
  flag.StringVar(&f_format,"format", "", "output file format (only plain is supported)")
  flag.StringVar(&f_host,"h", "", "hostname")
  flag.StringVar(&f_host,"host", "", "hostname")
  flag.StringVar(&f_port,"p", "", "port")
  flag.StringVar(&f_port,"port", "", "port")
  flag.StringVar(&f_username,"U","","username")
  flag.StringVar(&f_username,"username","","username")
  flag.BoolVar(  &f_no_password,"w",false,"never prompt for password")
  flag.BoolVar(  &f_no_password,"no-password",false,"never prompt for password")
  flag.BoolVar(  &f_password,"W",false,"force password prompt")
  flag.BoolVar(  &f_password,"password",false,"force password prompt")
  flag.StringVar(&f_schema,"n","","Dump the specified schema(s) only")
  flag.StringVar(&f_schema,"schema","","Dump the specified schema(s) only")
  flag.StringVar(&f_exclude_schema,"N","","Exclude the specified schema(s)")
  flag.StringVar(&f_exclude_schema,"exclude-schema","","Exclude the specified schema(s)")
  flag.StringVar(&f_table,"t","","Dump the specified table(s) only")
  flag.StringVar(&f_table,"table","","Dump the specified table(s) only")
  flag.BoolVar(&f_verbose,"v",false,"verbose mode")
  flag.BoolVar(&f_verbose,"verbose",false,"verbose mode")

  flag.Parse()

  // If there's a last remaining argument, it is DBNAME
  if flag.Arg(0) != "" {
    f_dbname=flag.Arg(0)
  }

  // If there's another argument, it is a problem
  if flag.Arg(1) != "" {
    log.Fatal("Do NOT place arguments after the DBNAME")
  }

  // options for both psql and pg_dump
  append_option("--dbname="   ,f_dbname)
  append_option("--host="     ,f_host)
  append_option("--port="     ,f_port)
  append_option("--username=" ,f_username)

  if f_no_password {
    append_option("--no-password")
  }

  if f_password {
    // instead of append_option("--password"), we define PGPASSWORD
    fmt.Print("Password: ")
    // syscall is required for Windows
    input, err := terminal.ReadPassword(int(os.Stdin.Fd()))
    if err != nil {
       log.Fatal(err)
    }
    err = os.Setenv("PGPASSWORD", string(input))
    if err != nil {
       log.Fatal(err)
    }
  }

  // options for the dsn
  append_dsn("host"   ,f_host)
  append_dsn("port"   ,f_port)
  append_dsn("user"   ,f_username)
  append_dsn("dbname" ,f_dbname)

  // options for pg_dump only
  append_pg_dump_option("--schema="         ,f_schema)
  append_pg_dump_option("--encoding="       ,f_encoding)
  append_pg_dump_option("--exclude-schema=" ,f_exclude_schema)
  append_pg_dump_option("--table="          ,f_table)
  append_pg_dump_option("--exclude-table="  ,f_exclude_table)

  if f_verbose {
    append_pg_dump_option("--verbose")
  }

  // Open the file ouput, if any
  if f_file != "" {
    fn := redirectOutput(f_file)
    defer fn()
  }


//##############################################################################
//## 1. Open the transaction / Get the snapshot name
//##############################################################################

  verbose("1. Open the transaction")

// As we will call pg_dump and psql multiple times to build the SQL dump,
// we need to use the same snapshot to guarantee data consistency.
// So we maintain an open transaction during the entire dump process and we pass
// its snapshot name to psql and pg_dump

  db, err := sql.Open("postgres", strings.Join(dsn," "))
  if err != nil {
      log.Fatal(dsn)
      panic(err)
  }
  defer db.Close()

  // Open a transaction in the background
  ctx := context.Background()
  tx, err := db.BeginTx(ctx, nil)
  defer tx.Rollback()

  if err == pq.ErrSSLNotSupported {
      verbose("disable sslmode and retry")
      dsn = append(dsn, "sslmode=disable")
      db, err = sql.Open("postgres", strings.Join(dsn," "))
      tx, err = db.BeginTx(ctx, nil)
  }

  if err != nil {
      log.Fatal(err)
  }

  // Get the snapshot name
  err = tx.QueryRowContext(ctx, sql_export_snapshot).Scan(&snapshotname)
  if err != nil {
      log.Fatal(err)
  }
  append_pg_dump_option("--snapshot=",snapshotname)

//##############################################################################
//## 2. Dump the pre-data section
//##############################################################################

  verbose("2. Dump the pre-data section")

  // Stop if the extension is not installed in the database
  var version string
  err = tx.QueryRow(sql_anon_version).Scan(&version)
  if err != nil {
    log.Fatal("Anon extension is not installed in this database.")
  }

  // Header
  fmt.Println("--")
  fmt.Printf( "-- Dump generated by PostgreSQL Anonymizer %s\n",version)
  fmt.Println("--")

  // gather all options needed to dump the DDL
  var mask_schema string
  err = tx.QueryRow(sql_mask_schema).Scan(&mask_schema)
  exclude_mask_schema := fmt.Sprintf("--exclude-schema=%s",mask_schema)
  ddl_dump_opt := []string{
    "--section=pre-data",                 // data will be dumped later
    "--no-security-labels",               // masking rules are confidential
    "--exclude-schema=anon",              // do not dump the extension schema
    exclude_mask_schema }
  pre_data := pg_dump_to_array(append(ddl_dump_opt,pg_dump_opts...))

  // We need to remove some `CREATE EXTENSION` commands
  // pg_dump 14 has an option `--extension` that would be useful
  // but for now we keep compatibility with older versions.
  pre_data_filtered := string(pre_data)
  pre_data_filtered = filter_out_extension(pre_data_filtered,"anon")
  pre_data_filtered = filter_out_extension(pre_data_filtered,"pgcrypto")
  fmt.Println(pre_data_filtered)

//##############################################################################
//## 3. Dump the tables data
//##############################################################################

  verbose("3. Dump the tables data")

  // We need to know which table data must be dumped.
  // So we parse the `pre-data` from the previous pg_dump
  re := regexp.MustCompile(`CREATE TABLE (.*) \(`)
  dumped_tables := re.FindAllSubmatch(pre_data,-1)

  // For each dumped table, we export the data by applying the masking rules
  var filters string
  var sample string
  for _, t := range dumped_tables {

    // 3a- get the table name
    // FindAllSubmatch returns 2 values for each match
    tablename := string(t[1])

    // 3b- Output the "COPY ... FROM STDIN" statement for the table
    fmt.Printf(sql_copy_from,tablename)

    // 3c- Output the masked data
    // get the masking filters of this table (if any)
    sql_mask_filters_for_table := fmt.Sprintf(sql_mask_filters, tablename)
    err = tx.QueryRow(sql_mask_filters_for_table).Scan(&filters)
    // write the data extraction statement
    sql_get_tablesample_ratio := fmt.Sprintf(sql_tablesample, tablename)
    err = tx.QueryRow(sql_get_tablesample_ratio).Scan(&sample)
    copy_to := fmt.Sprintf(sql_copy_to,snapshotname,filters,tablename,sample)
    copy_opt:=fmt.Sprintf("--command=%s", strings.ReplaceAll(copy_to, "\n", ""))
    // We're calling psql directly instead of using the opened transaction
    // because the output may be huge (potentially several TB)
    // and we want to throw it directly to stdout and avoid passing it through
    // golang's memory (especially on Windows)
    psql_copy_opts := append(psql_opts, copy_opt)
    verbose("psql "+ strings.Join(psql_copy_opts," "))
    cmd := exec.Command("psql", psql_copy_opts...)  // #nosec G204
    cmd.Stdout = os.Stdout
    err = cmd.Run()
    if ( err != nil ) {
      log.Fatal(err)
    }

    // 3d- Close the 'COPY FROM stdin' stream
    fmt.Println("\\.")
    fmt.Println("")
  }


//##############################################################################
//## 4. Dump the sequences data
//##############################################################################

  verbose("4. Dump the sequences data")

  // extract the names of all sequences
  var seq_table_opts []string
  verbose(sql_sequences)
  rows,err:=tx.Query(sql_sequences)
  if err != nil {
    log.Fatal(err)
  }
  defer rows.Close()

  for rows.Next() {
    var sequence string
    err := rows.Scan(&sequence)
    if err != nil {
      log.Fatal(err)
    }
    seq_table_opts = append(seq_table_opts,sequence)
  }

  if seq_table_opts != nil {
    // we only want the `setval` lines, so we use --data-only
    seq_data_dump_opts := []string{"--data-only"}
    seq_data_dump_opts = append(seq_data_dump_opts,seq_table_opts...)
    seq_data_dump_opts = append(seq_data_dump_opts,pg_dump_opts...)
    pg_dump(seq_data_dump_opts, output)
  }


//##############################################################################
//## 5. Dump the post-data section
//##############################################################################

  verbose("5. Dump the post-data section")

  post_data_dump_opts := []string{
    "--section=post-data",
    "--no-security-labels",  // masking rules are confidential
    "--exclude-schema=anon", // do not dump the extension schema
    exclude_mask_schema }    // define at the pre-data step

  pg_dump(append(post_data_dump_opts,pg_dump_opts...), output)


//##############################################################################
//## 6. Cleanup
//##############################################################################

  if tx != nil {
    // Close the transaction
    err := tx.Rollback()
    if err != nil {
        log.Fatal(err)
        os.Exit(1)
    }
  }

  if db != nil {
    // Close the session
    err = db.Close()
    if err != nil {
        log.Fatal(err)
        os.Exit(1)
    }
  }

  os.Exit(0)
}

