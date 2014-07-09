handler_desc='Checks for possibly invalidly mounted filesystems.'
handler_exec()
{
  msg=''
  while read -r dev mnt fs opts _; do
    [[ $fs =~ ^(ext[234]|tmpfs|xfs)$ ]] || continue

    bad=''
    while read -d, -r opt; do
      [[ $opt =~ ^noexec|nosuid|ro$ ]] || continue
      bad+=" $opt"
    done <<< "$opts,"

    [ -n "$bad" ] && \
      msg+=$'\n'"$dev ($fs) at $mnt (opts:$bad)"
  done < /proc/mounts

  if [ -n "$msg" ]; then
    dieplug '2' "Mount problems detected.$msg"
  else
    dieplug '0' "No problems detected."
  fi
}
