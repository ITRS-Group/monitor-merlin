handler_desc='Checks all locally mounted filesystems for low amounts of free space.'
handler_exec()
{
  PATH="/opt/plugins:$PATH"
  depchk check_disk

  check_disk -w 10% -c 5% -L -A -v
}
