# syntax: diecho <exit code> <exit text without exit state prefix>
# desc: Prints the final status text and prepends the exit state prefix
#       depending on which exit code it was called with.
diecho()
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
  printf '%s\n' "$text"

  exit "$code"
}

# syntax: syntax <text>
# desc: Prints text and exits. Fancy stuff.
syntax()
{
  printf '%s\n' "$1"
  exit 0
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

  diecho '3' "Missing dependencies.$deps"
}
