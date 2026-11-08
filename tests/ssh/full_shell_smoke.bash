#!/usr/bin/env bash
# Run with the freshly built Bash, not the Mac's system shell.
set -eu
[[ ${BASH_VERSINFO[0]} == 5 && ${BASH_VERSINFO[1]} == 3 ]]
[[ $BASH_VERSION == 5.3.20* ]]

word='PS5 Term'
if command -v printf >/dev/null; then
    result="$(printf '%s' "$word")"
else
    exit 11
fi
[[ $result == 'PS5 Term' ]]

case "$result" in
    'PS5 Term') result=matched ;;
    *) exit 12 ;;
esac
[[ $result == matched ]]

sum() { local total=0 n; for n in "$@"; do (( total += n )); done; printf '%d' "$total"; }
[[ $(sum 2 3 4) == 9 ]]
array=('a b' c)
[[ ${#array[@]} == 2 && ${array[0]} == 'a b' ]]
eval 'evaluated=yes'
[[ $evaluated == yes ]]

[[ $(printf 'one\ntwo\n' | while IFS= read -r line; do printf '<%s>' "$line"; done) == '<one><two>' ]]
[[ $(cat <<'END'
literal $word
END
) == 'literal $word' ]]
[[ $( (printf subshell) ) == subshell ]]
if (exit 23); then exit 13; else [[ $? == 23 ]]; fi
printf 'Bash 5.3.20 full-shell smoke PASS (reference runtime only)\n'
