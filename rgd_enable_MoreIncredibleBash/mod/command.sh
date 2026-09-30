#!/bin/sh
# M.I.B. release zips (tags up to V3.7.1) run /mod/command.sh from
# GEM -> M.I.B. -> Advanced Settings -> "Run individual script"; M.I.B. main runs
# /mod/custom.sh from "Run Custom Script". FAT cannot hold a symlink, so forward.
case ${0##*/} in
    command.sh) exec /bin/sh "${0%/*}/custom.sh" "$@" ;;
esac
