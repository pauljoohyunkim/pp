#!/bin/sh
# Self-test for the AES-CTR demo.
#
# The valgrind harness (`.pi/skills/memory-leak-test/run_valgrind_validation.sh`)
# runs this script when it exists, exporting:
#   PP_DEMO_BIN         wrapper that runs the demo binary under valgrind
#   PP_SELFTEST_TMPDIR  scratch directory for keys, inputs and outputs
# Both are optional, so the script also works standalone:
#   ./selftest.sh
set -u

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin=${PP_DEMO_BIN:-$here/aes_demo}

if [ -n "${PP_SELFTEST_TMPDIR:-}" ]; then
    tmp=$PP_SELFTEST_TMPDIR
    mkdir -p "$tmp"
else
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/aes_selftest.XXXXXX")
    trap 'rm -rf "$tmp"' EXIT
fi

fail() {
    echo "selftest: FAIL: $*" >&2
    exit 1
}

# A short plaintext (exercises a non-block-aligned tail) and a longer one (exercises
# the counter increment across several blocks).
printf 'The quick brown fox jumps over the lazy dog.' >"$tmp/short.bin"
head -c 1000 /dev/urandom >"$tmp/long.bin"

echo "selftest: round trips for 16/24/32-byte keys"
for plain in short long; do
    for klen in 16 24 32; do
        head -c "$klen" /dev/urandom >"$tmp/key.$klen"
        cipher="$tmp/cipher.$plain.$klen"
        clear="$tmp/clear.$plain.$klen"

        "$bin" e "$tmp/$plain.bin" "$tmp/key.$klen" "$cipher" >"$tmp/enc.$plain.$klen.log" 2>&1 ||
            fail "$plain/$klen: encryption exited non-zero (see $tmp/enc.$plain.$klen.log)"

        in_size=$(wc -c <"$tmp/$plain.bin")
        out_size=$(wc -c <"$cipher")
        [ "$out_size" -eq $((in_size + 12)) ] ||
            fail "$plain/$klen: ciphertext is $out_size bytes, expected $((in_size + 12)) (12-byte IV prefix)"

        "$bin" d "$cipher" "$tmp/key.$klen" "$clear" >"$tmp/dec.$plain.$klen.log" 2>&1 ||
            fail "$plain/$klen: decryption exited non-zero (see $tmp/dec.$plain.$klen.log)"

        cmp -s "$tmp/$plain.bin" "$clear" ||
            fail "$plain/$klen: round trip does not reproduce the plaintext"
    done
done

echo "selftest: rejecting an unsupported 17-byte key"
head -c 17 /dev/urandom >"$tmp/key.17"
rm -f "$tmp/cipher.17"
if "$bin" e "$tmp/short.bin" "$tmp/key.17" "$tmp/cipher.17" >"$tmp/key17.log" 2>&1; then
    fail "a 17-byte key was accepted"
fi
[ -e "$tmp/cipher.17" ] && fail "a 17-byte key still produced an output file"
grep -q "16, 24, or 32 bytes" "$tmp/key17.log" || fail "17-byte key rejection diagnostic is missing"

echo "selftest: rejecting a ciphertext without a complete IV"
head -c 11 /dev/urandom >"$tmp/short_cipher.bin"
if "$bin" d "$tmp/short_cipher.bin" "$tmp/key.16" "$tmp/clear.short_cipher" >"$tmp/short_cipher.log" 2>&1; then
    fail "a ciphertext shorter than the IV was accepted"
fi
grep -q "does not contain IV" "$tmp/short_cipher.log" || fail "missing-IV diagnostic is missing"

echo "selftest: a wrong key must not reproduce the plaintext"
"$bin" d "$tmp/cipher.long.32" "$tmp/key.16" "$tmp/clear.wrongkey" >"$tmp/wrongkey.log" 2>&1 ||
    fail "decryption with a wrong (but valid-length) key exited non-zero (see $tmp/wrongkey.log)"
cmp -s "$tmp/long.bin" "$tmp/clear.wrongkey" && fail "decryption with a wrong key reproduced the plaintext"

echo "selftest: ok"
