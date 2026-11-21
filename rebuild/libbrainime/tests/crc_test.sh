#!/bin/sh
# M4 deferred-CRC behaviour: corruption in a section verified at
# open (DICT/CLSREP/RNK/META) fails the open; corruption in a
# deferred section (TOKID/CONN/SUPP/BIGR) passes the open but the
# first conversion MUST fail -- unverified data is never used.
# usage: crc_test.sh <file.bid3> <bi-binary>
set -u
SRC=$1; BI=$2; TMP=$(mktemp -d)
fails=0
flip() { python3 -c "
import sys
d=bytearray(open('$SRC','rb').read()); d[$1]^=0xFF
open('$TMP/c.bid3','wb').write(bytes(d))"; }
try() { # $1=offset $2=expect(openfail|convfail)
  flip "$1"
  out=$("$BI" "$TMP/c.bid3" convert てがみをかきました 2>&1)
  case "$2" in
    openfail) case "$out" in *"cannot open"*) echo "PASS open rejected (off $1)";; *) echo "FAIL expected open rejection (off $1): $out"; fails=$((fails+1));; esac;;
    convfail) case "$out" in *"(no result)"*) echo "PASS conversion rejected (off $1)";; *) echo "FAIL expected conversion rejection (off $1): $out"; fails=$((fails+1));; esac;;
  esac
}
# section offsets are read from the directory, not hard-coded
python3 - "$SRC" > "$TMP/offs" << 'PYEOF'
import struct, sys
d=open(sys.argv[1],'rb').read()
for i in range(struct.unpack_from("<I",d,8)[0]):
    sid,off,ln,crc=struct.unpack_from("<IIII",d,16+16*i)
    print(sid, off+min(1000,ln//2))
PYEOF
while read -r sid off; do
  case "$sid" in
    1|3|7|8|9) try "$off" openfail;;
    2|4|5|6) try "$off" convfail;;
  esac
done < "$TMP/offs"
# sanity: the untouched file still converts
"$BI" "$SRC" convert てがみをかきました 2>/dev/null | grep -q "手紙を書きました" \
  && echo "PASS clean file converts" || { echo "FAIL clean file"; fails=$((fails+1)); }
rm -rf "$TMP"
echo "crc_test: $fails failures"
exit $fails
