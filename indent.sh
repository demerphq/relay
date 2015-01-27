#!/bin/sh

line_length=120

if [ -x /opt/local/bin/gnuindent ]; then
  INDENT=/opt/local/bin/gnuindent
else
  INDENT=indent
fi

case "$($INDENT --version 2>/dev/null)" in
*GNU*)
  (set -x; $INDENT -kr -nut --line-length $line_length "$@") 
  ;;
*)
  echo "$0: Must have GNU indent."
  exit 1
  ;;
esac

exit 0
