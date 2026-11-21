# 09_aes – AES-CTR Encryptor / Decryptor Demo

AES-128 CTR mode implemented in PP. The demo is a work-in-progress; key expansion and CTR loop are in place, the CLI is intentionally minimal for the language.

The file format matches the reference implementation in
`https://github.com/pauljoohyunkim/aes128`:
the plaintext is encrypted in CTR mode and a randomly generated 16-byte nonce/IV is **prepended to the ciphertext**. Decryption reads the first 16 bytes as the nonce and then decrypts in place. Encryption = decryption for CTR.

## Files
* `main.pp` – CLI entry, argument handling via `pp_get_argv`
* `aes.pp` – AES S-box, key schedule, primitives
* `aes.ppi` – PPI declarations for AES types
* `rand.pp` / `rand.ppi` – CSPRNG / random bytes helper
* `Makefile` – builds `aes_demo`

## Build

```bash
make
```

This ensures `bin/pplang` is built and links with `lib/pp_std`.

## Usage

The binary accepts a single mode word followed by the three positional arguments:

```
./aes_demo <mode> <input> <keyfile> <output>
```

* `mode` = `e` for encrypt, `d` for decrypt
* `input`  – plaintext file for encryption, ciphertext file for decryption
* `keyfile` – exactly 16 bytes, raw binary key
* `output` – destination file

Example:

```bash
# create a 16-byte key
head -c 16 /dev/urandom > key.bin

# encrypt
./aes_demo e plaintext.txt key.bin plaintext.enc

# decrypt
./aes_demo d plaintext.enc key.bin plaintext.dec

# verify
cmp plaintext.txt plaintext.dec && echo ok
```

The encrypted file begins with 16 random bytes – the nonce. No separate IV file is used. The same key must be used for decryption.

Notes
* The demo currently uses a simple random nonce via `rand_bytes`. For production use a CSPRNG and unique nonce per message is required.
* Argument parsing is intentionally trivial: the first argument selects mode, the remaining three are taken positionally. No option flags are supported.

## Clean

```bash
make clean
```
