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
