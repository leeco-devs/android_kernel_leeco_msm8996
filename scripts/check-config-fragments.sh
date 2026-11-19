#!/bin/sh
#
# check-config-fragments.sh - check that the CONFIG_ symbols of a config
# fragment exist in Kconfig and carry values their type accepts.
#
# A fragment is merged with merge_config.sh and then resolved by olddefconfig,
# which quietly throws away what it cannot use. confdata.c skips a symbol that
# sym_find() does not know at all, and only prints a warning - easily lost in a
# build log - for a value that does not match the symbol type. A fragment can
# therefore appear to enable something while the resulting .config never
# mentions it. This script reads the fragment itself, so the mistake is an
# error here instead of a silent no-op later.
#
# Usage: scripts/check-config-fragments.sh [-s] [-h] [FILE...]
#   -s, --strict   treat warnings as errors too
#   -h, --help     show this help
#
# Without FILE arguments the LeEco fragment set is checked,
# arch/arm64/configs/vendor/leeco/*.conf, which is merged on top of
# arch/arm64/configs/vendor/msm-perf_defconfig. Pass any other fragments, or a
# full defconfig, to check those instead.
#
# Exit status is 0 when nothing is wrong and 1 on any error.

set -u

usage()
{
	sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'
}

strict=0

while [ $# -gt 0 ]; do
	case $1 in
	-s|--strict)
		strict=1
		shift
		;;
	-h|--help)
		usage
		exit 0
		;;
	--)
		shift
		break
		;;
	-*)
		echo "$0: unknown option $1" >&2
		exit 1
		;;
	*)
		break
		;;
	esac
done

srctree=${srctree:-$(cd "$(dirname "$0")/.." && pwd)}

if [ $# -eq 0 ]; then
	set -- "$srctree"/arch/arm64/configs/vendor/leeco/*.conf
	if [ ! -e "$1" ]; then
		echo "$0: no fragment found under" \
			"arch/arm64/configs/vendor/leeco" >&2
		exit 1
	fi
fi

for file in "$@"; do
	if [ ! -r "$file" ]; then
		echo "$0: cannot read $file" >&2
		exit 1
	fi
done

index=$(mktemp) || exit 1
kconfigs=$(mktemp) || exit 1
trap 'rm -f "$index" "$kconfigs"' EXIT HUP INT TERM

find "$srctree" \( -name 'Kconfig' -o -name 'Kconfig.*' \) \
	-not -path '*/.git/*' -print > "$kconfigs"
if [ ! -s "$kconfigs" ]; then
	echo "$0: no Kconfig files found under $srctree" >&2
	exit 1
fi

# Collect every symbol Kconfig defines, with its type, whether it has a
# prompt, and where it was defined.
xargs awk '
function trim(s)
{
	sub(/^[ \t]+/, "", s)
	sub(/[ \t]+$/, "", s)
	return s
}

FNR == 1 {
	sym = ""
	inhelp = 0
}

BEGIN {
	sq = sprintf("%c", 39)
}

{
	indented = ($0 ~ /^[ \t]/)
	line = trim($0)

	# Help text is prose, not properties; skip it until the block ends.
	if (inhelp) {
		if (indented)
			next
		inhelp = 0
	}
	if (line ~ /^(help|---help---)$/) {
		inhelp = 1
		next
	}

	if (line ~ /^(menu)?config[ \t]+[A-Za-z0-9_]+$/) {
		split(line, fields, /[ \t]+/)
		sym = fields[2]
		if (!(sym in type)) {
			type[sym] = "undefined"
			prompt[sym] = 0
			defloc[sym] = FILENAME ":" FNR
		}
		next
	}

	# Anything else ends the current definition.
	if (line ~ /^(choice|endchoice|menu|endmenu|if|endif)([ \t]|$)/ ||
	    line ~ /^(comment|mainmenu|source|rsource|osource)([ \t]|$)/) {
		sym = ""
		next
	}
	if (sym == "")
		next

	if (line ~ /^(bool|boolean|def_bool)([ \t]|$)/)
		t = "bool"
	else if (line ~ /^(tristate|def_tristate)([ \t]|$)/)
		t = "tristate"
	else if (line ~ /^(int|def_int)([ \t]|$)/)
		t = "int"
	else if (line ~ /^(hex|def_hex)([ \t]|$)/)
		t = "hex"
	else if (line ~ /^(string|def_string)([ \t]|$)/)
		t = "string"
	else if (line ~ /^prompt[ \t]/ && (line ~ /"/ || index(line, sq))) {
		if (!prompt[sym])
			promptloc[sym] = FILENAME ":" FNR
		prompt[sym] = 1
		next
	} else
		next

	if (type[sym] == "undefined")
		type[sym] = t
	# A prompt may be written with either quote character.
	if (line ~ /"/ || index(line, sq)) {
		if (!prompt[sym])
			promptloc[sym] = FILENAME ":" FNR
		prompt[sym] = 1
	}
}

END {
	for (s in type) {
		where = prompt[s] ? promptloc[s] : defloc[s]
		printf "%s\t%s\t%d\t%s\n", s, type[s], prompt[s], where
	}
}' < "$kconfigs" > "$index" || exit 1

# Compare every symbol in the fragments against that table.
awk -F '\t' '
NR == FNR {
	type[$1] = $2
	prompt[$1] = $3
	where[$1] = $4
	next
}

function trim(s)
{
	sub(/^[ \t]+/, "", s)
	sub(/[ \t]+$/, "", s)
	return s
}

function report(kind, msg)
{
	printf "%s:%d: %s: CONFIG_%s %s\n", FILENAME, FNR, kind, sym, msg
	if (kind == "error")
		errors++
	else
		warnings++
}

{
	line = $0
	sub(/\r$/, "", line)

	# "# CONFIG_FOO is not set" is how kconfig spells =n.
	if (line ~ /^#[ \t]*CONFIG_[A-Za-z0-9_]+[ \t]+is not set[ \t]*$/) {
		s = line
		sub(/^#[ \t]*CONFIG_/, "", s)
		sub(/[ \t]+is not set[ \t]*$/, "", s)
		sym = s
		value = "n"
		kind = "n"
	} else if (line ~ /^CONFIG_[A-Za-z0-9_]+=/) {
		s = line
		sub(/^CONFIG_/, "", s)
		eq = index(s, "=")
		sym = substr(s, 1, eq - 1)
		value = substr(s, eq + 1)
		if (value == "y" || value == "m" || value == "n")
			kind = value
		else
			kind = "value"
	} else {
		next
	}

	count++

	if (!(sym in type)) {
		report("error", "is not defined by any Kconfig file")
		next
	}

	t = type[sym]
	if (t == "undefined") {
		report("error", "is defined without a type at " where[sym])
		next
	}

	if (t == "bool") {
		if (kind == "m")
			report("error", "is a bool and cannot be set to m")
		else if (kind == "value")
			report("error", "is a bool and cannot hold " value)
	} else if (t == "tristate") {
		if (kind == "value")
			report("error", "is a tristate and cannot hold " value)
	} else if (kind == "y" || kind == "m" || kind == "n") {
		report("error", "is a " t " and cannot be set to " kind)
	}

	if (prompt[sym] == 0)
		report("warning", "has no prompt at " where[sym] \
		       ", so olddefconfig may ignore this value")
}

END {
	printf "%d symbol(s) in %d file(s): %d error(s), %d warning(s)\n",
		count, files, errors, warnings
	exit (errors || (strict && warnings)) ? 1 : 0
}
' strict="$strict" files="$#" "$index" "$@"
