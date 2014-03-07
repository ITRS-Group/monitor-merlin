#!/bin/bash

# 140307 peklof@op5.com

# Default to op5 Monitor's livestatus socket path.
[ -n "$LS_SOCK" ] || LS_SOCK='/opt/monitor/var/rw/live'
# Sane default
IFS=$' \t\n'

syntax()
{
  [ -n "$2" ] && \
    printf 'ERROR: %s\n\n' "$2"

  msgdie "$1" \
"Usage: mon api ls [--help|[<table> [OPTION|FILTER]..]

All available tables will be listed if no arguments are seen.

The first argument is always the table name. The table is described if this is
the one and only argument given.

Generic options:
 -h, --help      This syntax help text.
 -v              Increase verbosity (print raw filter).

Query options:
 -H              Add a leading set of column descriptions.
 -O <format>     Which format to output the results in. Defaults to \"csv\".
                 Available formats: csv, json, python, wrapped_json
 -S <n,n,n,n>    The four decimal ASCII values used as the dataset, the column,
                 the list, and the list-object separator in CSV output,
                 respectively. Tip: Find decimal values using this command:
                   printf '%d,%d,%d,%d\n' \'$'\n' \'\; \', \'\|
                 This generates \"10,59,44,124\", which is also the default.
                 To change only the 1st separator (dataset), only <n> is needed.
                 To change only the 2nd (column) and the 3rd (list): ,<n>,<n>

 -c <c1,cN>      Which columns to fetch. Defaults to all.
 -l <num>        Limit the number of displayed results. Defaults to all.

Filter definitions:
 <c> <op> <v>    Test-syntax. This method partly mimics the shell test syntax.
 <c><op><v>      LQL-syntax. This method is using ordinary LQL operators.

Filter operators (Test, LQL):
 -e, =           Column is equal to value.
 -ne, !=         Column is not equal to value.
 -ei, =~         Column is equal to value (case-insensitive).
 -nei, !=~       Column is not equal to value (case-insensitive).

 -r, ~           Column matches regex.
 -nr, !~         Column does not match regex.
 -ri, ~~         Column matches regex (case-insensitive).
 -nri, !~~       Column does not match regex (case-insensitive).

 -ge, >=         Column is greater or equal to value.
                 NOTE: '-ge' or '>=' is also used to match entries in lists.
 -gt, >          Column is greater than value.
 -le, <=         Column is less than or equal to value.
 -lt, <          Column is less than value.

Logical operations:
 -o <num>        OR the last <num> filters.
 -a <num>        AND the last <num> filters (to group and then OR).
 -n              Negate the last filter.

Examples:
 mon api ls hosts
 mon api ls hosts -c address name=darkstar
 mon api ls hosts -c name name -r ^a name~^b -o 2
 mon api ls hosts -c name state -ne 0 acknowledged -e 1 -a2 state=0 -o2 -l3
 mon api ls hosts -c name parents -e ''
 mon api ls hosts -c name parents\>=monitor
 mon api ls services -c host_name,description,state,plugin_output state -gt 0
"
}


# SNTX: ls_q <raw_livestatus_query>
# DESC: Perform the given ls query and print the result.
ls_q()
{
  printf '%s\n' "$1" | unixcat "$LS_SOCK" || \
    msgdie "$?" "Failed livestatus socket communication."
}

# SNTX: ls_qres <raw_livestatus_query>
# DESC: Perform the given ls query. The response data will be stored in $QRES.
ls_qres()
{
  QRES="$(printf '%s\n' "$1" | unixcat "$LS_SOCK" 2>&1)" || \
    msgdie "$?" "Failed livestatus socket communication: $QRES"
}

# SNTX: table_load <table_name>
# DESC: Fetch table information and store it in $TABLE_INFO.
table_load()
{
  local QRES table tables

  ls_qres $'GET columns\nColumns: name type description\n'"Filter: table = $1"

  if [ -n "$QRES" ]; then
    TABLE_INFO="$QRES"
    return 0
  else
    return 1
  fi
}

# SNTX: display_tables
# DESC: Print a list of all livestatus tables.
display_tables()
{
  local QRES table tables

  ls_qres $'GET columns\nColumns: table'

  tables="$(printf '%s\n' "$QRES" | sort | uniq)"

  for table in $tables; do
    printf '%s\n' "$table"
  done
}

# SNTX: display_columns
# DESC: Print the pre-loaded table's columns with types and descriptions.
display_columns()
{
  local desc max_name max_type name type

  max_name='0'
  max_type='0'

  while IFS=';' read -r name type _; do
    [ "${#name}" -gt "$max_name" ] && max_name="${#name}"
    [ "${#type}" -gt "$max_type" ] && max_type="${#type}"
  done < <(printf '%s\n' "$TABLE_INFO")

  while IFS=';' read -r name type desc; do
   printf "%${max_type}s  %-${max_name}s  %s\n" "$type" "$name" "$desc"
  done < <(printf '%s\n' "$TABLE_INFO")
}


# SNTX: filter_is_raw <string>
# DESC: Returns 0 if the given string matches <col><op><val> filter type.
#       If true, $BASH_REMATCH[1-3] contains the three different matches.
filter_is_raw()
{
  local r
  r='^([a-z]+[0-9a-z_]*[0-9a-z]+)(!?=~?|!?~~?|[<>]=?)(.*)$'
  [[ $1 =~ $r ]] && return 0 || return 1
}

# SNTX: verify_and_set_separators <1,2,3,4>
# DESC: Verifies that any given separators are values between 0-255 (i.e. just
#       like a single byte) and returns $CSVSEP array containing 4 items.
verify_and_set_separators()
{
  local n sep

  CSVSEP=('10' '59' '44' '124')
  n='0'
  while read -r -d',' sep; do
    if [ -n "$sep" ]; then
      if ! [ "$sep" -ge '0' -a "$sep" -le '255' ] &> /dev/null; then
        syntax '1' "Invalid CSV separator specified ($sep)."
      fi
      CSVSEP[$n]="$sep"
    fi
    let n++
    [ "$n" -ge '3' ] && break
  done < <(printf '%s,' "$1")
}

# SNTX: verify_and_translate_operator <cmdline_operator_switch>
# DESC: Translates a command line operator switch into the corresponding
#       LQL filter version and stores it into $FILTEROP.
verify_and_translate_operator()
{
  case "$1" in
    -e)
      FILTEROP='=' ;;
    -ne)
      FILTEROP='!=' ;;
    -ei)
      FILTEROP='=~' ;;
    -nei)
      FILTEROP='!=~' ;;
    -r)
      FILTEROP='~' ;;
    -nr)
      FILTEROP='!~' ;;
    -ri)
      FILTEROP='~~' ;;
    -nri)
      FILTEROP='!~~' ;;
    -le)
      FILTEROP='<=' ;;
    -lt)
      FILTEROP='<' ;;
    -ge)
      FILTEROP='>=' ;;
    -gt)
      FILTEROP='>' ;;
    *)
      syntax '1' "Invalid filter operator specified ($1)."
  esac
}

# SNTX: verify_arg2_not_empty <arg1> <arg2> [argN]..
# DESC: Verifies that at least two arguments were given and are non-empty.
verify_arg2_not_empty()
{
  [ -n "$2" ] || \
    syntax '1' "$1: second argument is missing or empty."
}

# SNTX: verify_arg3_defined <arg1> <arg2> <arg3> [argN]..
# DESC: Verifies that at least three arguments were given.
verify_arg3_defined()
{
  [ "${3+defined}" = 'defined' ] || \
    syntax '1' "$1 $2: third argument is missing."
}

# SNTX: verify_column_exists <column_name>
# DESC: Verifies that the given column name exists in the pre-loaded table.
verify_column_exists()
{
  local name

  while IFS=';' read -r name _; do
    [ "$name" == "$1" ] && return 0
  done < <(printf '%s\n' "$TABLE_INFO")

  msgdie '1' "Column ($1) not found in table ($TABLE_NAME)."
}

# SNTX: verify_columns <comma_separated_list_of_column_names>
# DESC: Verifies each given column name using verify_column_exists().
verify_columns()
{
  local IFS column

  IFS=' '
  for column in ${1//,/ }; do
    verify_column_exists "$column"
  done
}

# SNTX: verify_non_zero_number <number>
# DESC: Verifies that the given number is greater than 1.
verify_non_zero_number()
{
  [[ $1 =~ ^[1-9][0-9]*$ ]] || \
    msgdie '1' "Invalid number specified ($1)."
}

# SNTX: verify_output_format <format_string>
# DESC: Verifies that the given string is a valid format name.
verify_output_format()
{
  [[ $1 =~ ^(csv|json|python|wrapped_json)$ ]] || \
    syntax '1' "Invalid output format specified ($1)."
}

# SNTX: verify_raw_filter <lql_filter_definition>
# DESC: Verifies that the given string looks like an LQL filter definition.
verify_raw_filter()
{
  local r

  r='^[^ ]+ +[^ ]+ +[^ ]+$'
  [[ $1 =~ $r ]] || \
    syntax '1' "Invalid filter definition ($1)."
}


# Get filesystem dirpath of $0, depending on executed using $PATH or not.
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# Include the generic mon bash module functions.
if [ -f "$d/bash/inc.sh" ]; then
  . "$d/bash/inc.sh"
else
  printf 'File not found: %s\n' "$d/bash/inc.sh"
  exit 1
fi

# Disable unexpected pathname expansion.
set -f

# Make sure that all external binary deps exists.
depchk 'sort' 'uniq' 'unixcat'


# Show all tables and quit if no arguments were given.
if [ "$#" == '0' ]; then
  display_tables
  exit 0
fi

# Syntax help text requested?
[ "$1" == '--help' -o "$1" == '-h' ] && \
  syntax 0

# The first argument is always the table name.
TABLE_NAME="$1"
shift

# Fetch the table's column descriptions.
table_load "$TABLE_NAME" || \
  msgdie '1' "Table not found: $TABLE_NAME"

# Describe the table and quit if no further arguments given.
if [ "$#" == '0' ]; then
  display_columns "$TABLE_NAME"
  exit 0
fi


# Set sane argument defaults.
VERBOSE='0'
# ... and allocate space for non-filter options in query array.
Q=("GET $TABLE_NAME" '' '' '' '')

# Handle arguments.
while [ "$#" -gt '0' ]; do
  case "$1" in
    # Non-query options...
    -h|--help)
      syntax 0
      ;;
    -v)
      let VERBOSE+=1
      shift 1
      ;;

    # Query options...
    -H)
      Q[2]='ColumnHeaders: on'
      shift 1
      ;;
    -O)
      verify_arg2_not_empty "$@"
      verify_output_format "$2"
      Q[3]="OutputFormat: $2"
      shift 2
      ;;
    -S)
      verify_arg2_not_empty "$@"
      verify_and_set_separators "$2"
      Q[3]="Separators: ${CSVSEP[*]}"
      shift 2
      ;;
    -c)
      verify_arg2_not_empty "$@"
      verify_columns "$2"
      Q[1]="Columns: ${2//,/ }"
      shift 2
      ;;
    -l)
      verify_arg2_not_empty "$@"
      verify_non_zero_number "$2"
      Q[4]="Limit: $2"
      shift 2
      ;;
    -l*)
      verify_arg2_not_empty '-l' "${1:2}"
      verify_non_zero_number "${1:2}"
      Q[4]="Limit: ${1:2}"
      shift 1
      ;;

    # Query filter contruction parameters...
    -a)
      verify_arg2_not_empty "$@"
      verify_non_zero_number "$2"
      Q+=("And: $2")
      shift 2
      ;;
    -a*)
      verify_arg2_not_empty '-a' "${1:2}"
      verify_non_zero_number "${1:2}"
      Q+=("And: ${1:2}")
      shift 1
      ;;
    -o)
      verify_arg2_not_empty "$@"
      verify_non_zero_number "$2"
      Q+=("Or: $2")
      shift 2
      ;;
    -o*)
      verify_arg2_not_empty '-o' "${1:2}"
      verify_non_zero_number "${1:2}"
      Q+=("Or: ${1:2}")
      shift 1
      ;;
    -n)
      Q+=("Negate: $2")
      shift 1
      ;;
    *)
      if filter_is_raw "$1"; then
        FILTERCOL="${BASH_REMATCH[1]}"
        FILTEROP="${BASH_REMATCH[2]}"
        FILTERVAL="${BASH_REMATCH[3]}"
        shift 1
      else
        verify_arg2_not_empty "$@"
        verify_arg3_defined "$@"
        FILTERCOL="$1"
        verify_and_translate_operator "$2"
        FILTERVAL="$3"
        shift 3
      fi
      verify_column_exists "$FILTERCOL"

      Q+=("Filter: $FILTERCOL $FILTEROP $FILTERVAL")
      ;;
  esac
done

# Transform the query array into a multi line string.
LSQ=''
for i in ${!Q[@]}; do
  [ -n "${Q[$i]}" ] || continue
  LSQ+="${Q[$i]}"$'\n'
done

[ $VERBOSE -gt 0 ] && \
  printf '=QUERY:START\n%s=QUERY:END\n' "$LSQ"

ls_q "$LSQ"
exit 0
