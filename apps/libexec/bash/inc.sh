# SNTX: msgdie <exit code> <text>
# DESC: Prints specified text and exits with given exit code.
msgdie()
{
  printf '%s\n' "$2"
  exit $1
}

# syntax: diecho <exit code> <exit text without exit state prefix>
# desc: Prints the final status text and prepends the exit state prefix
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

# syntax: depchk <dep1> [depN]..
# desc: Checks if all specified executables are available via $PATH
#       - exits if not.
depchk()
{
  local arg dep deps i

  dep=()
  for arg; do
    hash "$arg" || dep+=("$arg")
  done &> /dev/null

  [ "${#dep[@]}" == '0' ] && return 0

  for i in "${!dep[@]}"; do
    deps+=$'\n'"dep: ${dep[$i]}"
  done

  msgdie '3' "Missing dependencies.$deps"
}
