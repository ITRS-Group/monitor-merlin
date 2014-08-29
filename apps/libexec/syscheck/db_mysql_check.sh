handler_desc='Detects errors in the merlin and nacoma database using mysqlcheck.'
handler_exec()
{
  depchk grep mysqlcheck
  # Sourced cmd_exec() will execute the given command line and store the output
  # text in $OUT. Should the command not return zero, the script will exit.
  cmd_exec mysqlcheck -q -s --databases merlin nacoma

  errors="$(printf '%s\n' "$OUT" | grep -Ei -B1 '^error[[:space:]]*:')"

  if [ -z "$errors" ]; then
    ret='0'
    msg='No errors detected.'
  else
    ret='2'
    msg='Error(s) detected.'
    msg+=$'\n'"$OUT"
  fi

  dieplug "$ret" "$msg"
}
