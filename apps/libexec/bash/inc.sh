# SNTX: msgdie <exit code> <text>
# DESC: Prints specified text and exits with given exit code.
msgdie()
{
  printf '%s\n' "$2"
  exit $1
}

# SNTX: dieplug <exit code> <exit text without exit state prefix>
# DESC: Prints the final status text and prepends the exit state prefix
#       depending on which exit code it was called with.
dieplug()
{
  local code text prefix
  code="$1"

  case "$code" in
    '0')
      text='OK'
      ;;
    '1')
      text='WARNING'
      ;;
    '2')
      text='CRITICAL'
      ;;
    '3')
      text='UNKNOWN'
      ;;
    *)
      text="INVALID exit code ($code)"
      code='3'
      ;;
  esac

  # append any additional text if specified
  [ -n "$2" ] && text+=": $2"

  # print text to stdout
  msgdie "$code" "$text"
}

# SNTX: depchk <dep1> [depN]..
# DESC: Exits in case any given dependencies are missing.
depchk()
{
  local arg miss

  miss=''
  for arg; do
    hash -- "$arg" &> /dev/null || miss+=" $arg"
  done

  [ -n "$miss" ] || return 0

  msgdie '3' "Missing dependencies:$miss"
}


# SNTX: cmd_exec <command> [arg1] [argN]..
# DESC: Executes given command line, which if it fails, exits with debug output.
cmd_exec()
{
  OUT="$("$@" 2>&1)"
  RET="$?"

  [ "$RET" == '0' ] || \
    msgdie '3' "$(cmd_debug_output "$@")"

  return 0
}

# SNTX: [OUT=<text>] [RET=<$?>] cmd_debug_output <command> [arg1] [argN]..
# DESC: Verbosely describes the given command line and possible results.
cmd_debug_output()
{
  local args
  args="$(for arg; do printf '(%s)\n' "$arg"; done)"

  printf 'Executed command failed unexpectedly.\n'
  cmd_debug_section 'CMD LINE' "$*"
  cmd_debug_section 'ARGUMENTS' "$args"
  [ -n "$RET" ] && cmd_debug_section 'RETURN CODE' "$RET"
  [ -n "$OUT" ] && cmd_debug_section 'OUTPUT' "$OUT"
}

# SNTX: cmd_debug_section <tag> <text>
# DESC: Encapsulates given text with tagged header and footer.
cmd_debug_section()
{
  printf '\n'
  printf '==%s:START==\n' "$1"
  printf '%s\n' "$2"
  printf '==%s:END==\n' "$1"
}
