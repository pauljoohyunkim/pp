let sbox :: const [uint8 : 256u] = [uint8 : 256u;
    0X63uss, 0X7Cuss, 0X77uss, 0X7Buss, 0XF2uss, 0X6Buss, 0X6Fuss, 0XC5uss, 0X30uss, 0X01uss, 0X67uss, 0X2Buss, 0XFEuss, 0XD7uss, 0XABuss, 0X76uss,
	0XCAuss, 0X82uss, 0XC9uss, 0X7Duss, 0XFAuss, 0X59uss, 0X47uss, 0XF0uss, 0XADuss, 0XD4uss, 0XA2uss, 0XAFuss, 0X9Cuss, 0XA4uss, 0X72uss, 0XC0uss,
	0XB7uss, 0XFDuss, 0X93uss, 0X26uss, 0X36uss, 0X3Fuss, 0XF7uss, 0XCCuss, 0X34uss, 0XA5uss, 0XE5uss, 0XF1uss, 0X71uss, 0XD8uss, 0X31uss, 0X15uss,
	0X04uss, 0XC7uss, 0X23uss, 0XC3uss, 0X18uss, 0X96uss, 0X05uss, 0X9Auss, 0X07uss, 0X12uss, 0X80uss, 0XE2uss, 0XEBuss, 0X27uss, 0XB2uss, 0X75uss,
	0X09uss, 0X83uss, 0X2Cuss, 0X1Auss, 0X1Buss, 0X6Euss, 0X5Auss, 0XA0uss, 0X52uss, 0X3Buss, 0XD6uss, 0XB3uss, 0X29uss, 0XE3uss, 0X2Fuss, 0X84uss,
	0X53uss, 0XD1uss, 0X00uss, 0XEDuss, 0X20uss, 0XFCuss, 0XB1uss, 0X5Buss, 0X6Auss, 0XCBuss, 0XBEuss, 0X39uss, 0X4Auss, 0X4Cuss, 0X58uss, 0XCFuss,
	0XD0uss, 0XEFuss, 0XAAuss, 0XFBuss, 0X43uss, 0X4Duss, 0X33uss, 0X85uss, 0X45uss, 0XF9uss, 0X02uss, 0X7Fuss, 0X50uss, 0X3Cuss, 0X9Fuss, 0XA8uss,
	0X51uss, 0XA3uss, 0X40uss, 0X8Fuss, 0X92uss, 0X9Duss, 0X38uss, 0XF5uss, 0XBCuss, 0XB6uss, 0XDAuss, 0X21uss, 0X10uss, 0XFFuss, 0XF3uss, 0XD2uss,
	0XCDuss, 0X0Cuss, 0X13uss, 0XECuss, 0X5Fuss, 0X97uss, 0X44uss, 0X17uss, 0XC4uss, 0XA7uss, 0X7Euss, 0X3Duss, 0X64uss, 0X5Duss, 0X19uss, 0X73uss,
	0X60uss, 0X81uss, 0X4Fuss, 0XDCuss, 0X22uss, 0X2Auss, 0X90uss, 0X88uss, 0X46uss, 0XEEuss, 0XB8uss, 0X14uss, 0XDEuss, 0X5Euss, 0X0Buss, 0XDBuss,
	0XE0uss, 0X32uss, 0X3Auss, 0X0Auss, 0X49uss, 0X06uss, 0X24uss, 0X5Cuss, 0XC2uss, 0XD3uss, 0XACuss, 0X62uss, 0X91uss, 0X95uss, 0XE4uss, 0X79uss,
	0XE7uss, 0XC8uss, 0X37uss, 0X6Duss, 0X8Duss, 0XD5uss, 0X4Euss, 0XA9uss, 0X6Cuss, 0X56uss, 0XF4uss, 0XEAuss, 0X65uss, 0X7Auss, 0XAEuss, 0X08uss,
	0XBAuss, 0X78uss, 0X25uss, 0X2Euss, 0X1Cuss, 0XA6uss, 0XB4uss, 0XC6uss, 0XE8uss, 0XDDuss, 0X74uss, 0X1Fuss, 0X4Buss, 0XBDuss, 0X8Buss, 0X8Auss,
	0X70uss, 0X3Euss, 0XB5uss, 0X66uss, 0X48uss, 0X03uss, 0XF6uss, 0X0Euss, 0X61uss, 0X35uss, 0X57uss, 0XB9uss, 0X86uss, 0XC1uss, 0X1Duss, 0X9Euss,
	0XE1uss, 0XF8uss, 0X98uss, 0X11uss, 0X69uss, 0XD9uss, 0X8Euss, 0X94uss, 0X9Buss, 0X1Euss, 0X87uss, 0XE9uss, 0XCEuss, 0X55uss, 0X28uss, 0XDFuss,
	0X8Cuss, 0XA1uss, 0X89uss, 0X0Duss, 0XBFuss, 0XE6uss, 0X42uss, 0X68uss, 0X41uss, 0X99uss, 0X2Duss, 0X0Fuss, 0XB0uss, 0X54uss, 0XBBuss, 0X16uss
];

let rc :: const [uint8 : 11u] = [uint8 : 11u; 
    0x00uss, 0x01uss, 0x02uss, 0x04uss, 0x08uss, 0x10uss, 0x20uss, 0x40uss, 0x80uss, 0x1Buss, 0x36uss
];

let g_table_2 :: const [uint8 : 256u] = [uint8 : 256u;
    0x00uss, 0x02uss, 0x04uss, 0x06uss, 0x08uss, 0x0auss, 0x0cuss, 0x0euss, 0x10uss, 0x12uss, 0x14uss, 0x16uss, 0x18uss, 0x1auss, 0x1cuss, 0x1euss, 
    0x20uss, 0x22uss, 0x24uss, 0x26uss, 0x28uss, 0x2auss, 0x2cuss, 0x2euss, 0x30uss, 0x32uss, 0x34uss, 0x36uss, 0x38uss, 0x3auss, 0x3cuss, 0x3euss, 
    0x40uss, 0x42uss, 0x44uss, 0x46uss, 0x48uss, 0x4auss, 0x4cuss, 0x4euss, 0x50uss, 0x52uss, 0x54uss, 0x56uss, 0x58uss, 0x5auss, 0x5cuss, 0x5euss, 
    0x60uss, 0x62uss, 0x64uss, 0x66uss, 0x68uss, 0x6auss, 0x6cuss, 0x6euss, 0x70uss, 0x72uss, 0x74uss, 0x76uss, 0x78uss, 0x7auss, 0x7cuss, 0x7euss, 	
    0x80uss, 0x82uss, 0x84uss, 0x86uss, 0x88uss, 0x8auss, 0x8cuss, 0x8euss, 0x90uss, 0x92uss, 0x94uss, 0x96uss, 0x98uss, 0x9auss, 0x9cuss, 0x9euss, 
    0xa0uss, 0xa2uss, 0xa4uss, 0xa6uss, 0xa8uss, 0xaauss, 0xacuss, 0xaeuss, 0xb0uss, 0xb2uss, 0xb4uss, 0xb6uss, 0xb8uss, 0xbauss, 0xbcuss, 0xbeuss, 
    0xc0uss, 0xc2uss, 0xc4uss, 0xc6uss, 0xc8uss, 0xcauss, 0xccuss, 0xceuss, 0xd0uss, 0xd2uss, 0xd4uss, 0xd6uss, 0xd8uss, 0xdauss, 0xdcuss, 0xdeuss, 
    0xe0uss, 0xe2uss, 0xe4uss, 0xe6uss, 0xe8uss, 0xeauss, 0xecuss, 0xeeuss, 0xf0uss, 0xf2uss, 0xf4uss, 0xf6uss, 0xf8uss, 0xfauss, 0xfcuss, 0xfeuss, 
    0x1buss, 0x19uss, 0x1fuss, 0x1duss, 0x13uss, 0x11uss, 0x17uss, 0x15uss, 0x0buss, 0x09uss, 0x0fuss, 0x0duss, 0x03uss, 0x01uss, 0x07uss, 0x05uss, 
    0x3buss, 0x39uss, 0x3fuss, 0x3duss, 0x33uss, 0x31uss, 0x37uss, 0x35uss, 0x2buss, 0x29uss, 0x2fuss, 0x2duss, 0x23uss, 0x21uss, 0x27uss, 0x25uss, 
    0x5buss, 0x59uss, 0x5fuss, 0x5duss, 0x53uss, 0x51uss, 0x57uss, 0x55uss, 0x4buss, 0x49uss, 0x4fuss, 0x4duss, 0x43uss, 0x41uss, 0x47uss, 0x45uss, 
    0x7buss, 0x79uss, 0x7fuss, 0x7duss, 0x73uss, 0x71uss, 0x77uss, 0x75uss, 0x6buss, 0x69uss, 0x6fuss, 0x6duss, 0x63uss, 0x61uss, 0x67uss, 0x65uss, 
    0x9buss, 0x99uss, 0x9fuss, 0x9duss, 0x93uss, 0x91uss, 0x97uss, 0x95uss, 0x8buss, 0x89uss, 0x8fuss, 0x8duss, 0x83uss, 0x81uss, 0x87uss, 0x85uss, 
    0xbbuss, 0xb9uss, 0xbfuss, 0xbduss, 0xb3uss, 0xb1uss, 0xb7uss, 0xb5uss, 0xabuss, 0xa9uss, 0xafuss, 0xaduss, 0xa3uss, 0xa1uss, 0xa7uss, 0xa5uss, 
    0xdbuss, 0xd9uss, 0xdfuss, 0xdduss, 0xd3uss, 0xd1uss, 0xd7uss, 0xd5uss, 0xcbuss, 0xc9uss, 0xcfuss, 0xcduss, 0xc3uss, 0xc1uss, 0xc7uss, 0xc5uss, 
    0xfbuss, 0xf9uss, 0xffuss, 0xfduss, 0xf3uss, 0xf1uss, 0xf7uss, 0xf5uss, 0xebuss, 0xe9uss, 0xefuss, 0xeduss, 0xe3uss, 0xe1uss, 0xe7uss, 0xe5uss
];

let g_table_3 :: const [uint8 : 256u] = [uint8 : 256u;
    0x00uss, 0x03uss, 0x06uss, 0x05uss, 0x0cuss, 0x0fuss, 0x0auss, 0x09uss, 0x18uss, 0x1buss, 0x1euss, 0x1duss, 0x14uss, 0x17uss, 0x12uss, 0x11uss,
    0x30uss, 0x33uss, 0x36uss, 0x35uss, 0x3cuss, 0x3fuss, 0x3auss, 0x39uss, 0x28uss, 0x2buss, 0x2euss, 0x2duss, 0x24uss, 0x27uss, 0x22uss, 0x21uss,
    0x60uss, 0x63uss, 0x66uss, 0x65uss, 0x6cuss, 0x6fuss, 0x6auss, 0x69uss, 0x78uss, 0x7buss, 0x7euss, 0x7duss, 0x74uss, 0x77uss, 0x72uss, 0x71uss,
    0x50uss, 0x53uss, 0x56uss, 0x55uss, 0x5cuss, 0x5fuss, 0x5auss, 0x59uss, 0x48uss, 0x4buss, 0x4euss, 0x4duss, 0x44uss, 0x47uss, 0x42uss, 0x41uss,
    0xc0uss, 0xc3uss, 0xc6uss, 0xc5uss, 0xccuss, 0xcfuss, 0xcauss, 0xc9uss, 0xd8uss, 0xdbuss, 0xdeuss, 0xdduss, 0xd4uss, 0xd7uss, 0xd2uss, 0xd1uss,
    0xf0uss, 0xf3uss, 0xf6uss, 0xf5uss, 0xfcuss, 0xffuss, 0xfauss, 0xf9uss, 0xe8uss, 0xebuss, 0xeeuss, 0xeduss, 0xe4uss, 0xe7uss, 0xe2uss, 0xe1uss,
    0xa0uss, 0xa3uss, 0xa6uss, 0xa5uss, 0xacuss, 0xafuss, 0xaauss, 0xa9uss, 0xb8uss, 0xbbuss, 0xbeuss, 0xbduss, 0xb4uss, 0xb7uss, 0xb2uss, 0xb1uss,
    0x90uss, 0x93uss, 0x96uss, 0x95uss, 0x9cuss, 0x9fuss, 0x9auss, 0x99uss, 0x88uss, 0x8buss, 0x8euss, 0x8duss, 0x84uss, 0x87uss, 0x82uss, 0x81uss,
    0x9buss, 0x98uss, 0x9duss, 0x9euss, 0x97uss, 0x94uss, 0x91uss, 0x92uss, 0x83uss, 0x80uss, 0x85uss, 0x86uss, 0x8fuss, 0x8cuss, 0x89uss, 0x8auss,
    0xabuss, 0xa8uss, 0xaduss, 0xaeuss, 0xa7uss, 0xa4uss, 0xa1uss, 0xa2uss, 0xb3uss, 0xb0uss, 0xb5uss, 0xb6uss, 0xbfuss, 0xbcuss, 0xb9uss, 0xbauss,
    0xfbuss, 0xf8uss, 0xfduss, 0xfeuss, 0xf7uss, 0xf4uss, 0xf1uss, 0xf2uss, 0xe3uss, 0xe0uss, 0xe5uss, 0xe6uss, 0xefuss, 0xecuss, 0xe9uss, 0xeauss,
    0xcbuss, 0xc8uss, 0xcduss, 0xceuss, 0xc7uss, 0xc4uss, 0xc1uss, 0xc2uss, 0xd3uss, 0xd0uss, 0xd5uss, 0xd6uss, 0xdfuss, 0xdcuss, 0xd9uss, 0xdauss,
    0x5buss, 0x58uss, 0x5duss, 0x5euss, 0x57uss, 0x54uss, 0x51uss, 0x52uss, 0x43uss, 0x40uss, 0x45uss, 0x46uss, 0x4fuss, 0x4cuss, 0x49uss, 0x4auss,
    0x6buss, 0x68uss, 0x6duss, 0x6euss, 0x67uss, 0x64uss, 0x61uss, 0x62uss, 0x73uss, 0x70uss, 0x75uss, 0x76uss, 0x7fuss, 0x7cuss, 0x79uss, 0x7auss,
    0x3buss, 0x38uss, 0x3duss, 0x3euss, 0x37uss, 0x34uss, 0x31uss, 0x32uss, 0x23uss, 0x20uss, 0x25uss, 0x26uss, 0x2fuss, 0x2cuss, 0x29uss, 0x2auss,
    0x0buss, 0x08uss, 0x0duss, 0x0euss, 0x07uss, 0x04uss, 0x01uss, 0x02uss, 0x13uss, 0x10uss, 0x15uss, 0x16uss, 0x1fuss, 0x1cuss, 0x19uss, 0x1auss
];

typedef [uint8 : 4u] WORD;

func RotWord :: (word :: WORD&) -> void {
    let temp :: uint8 = word[0u];
    word[0u] = word[1u];
    word[1u] = word[2u];
    word[2u] = word[3u];
    word[3u] = temp;
}

func SubWord :: (word :: WORD&) -> void {
    for (let i :: uint32 = 0u; i < #word as uint32; i += 1u) {
        word[i] = sbox[word[i]];
    }
}

lambda rcon :: (i :: uint32) -> WORD {
    return [uint8 : 4u; rc[i]];
}

lambda read_word_from_flattened_array :: (buf :: const [uint8 : AES_EXPANDED_KEY_BYTES]&, idx :: uint32) -> WORD {
    let word :: WORD;
    word[0u] = buf[idx];
    word[1u] = buf[idx+1u];
    word[2u] = buf[idx+2u];
    word[3u] = buf[idx+3u];

    return word;
}

lambda XorWord :: (w1 :: const WORD&, w2 :: const WORD&) -> WORD {
    let word :: WORD;
    word[0u] = w1[0u] ^ w2[0u];
    word[1u] = w1[1u] ^ w2[1u];
    word[2u] = w1[2u] ^ w2[2u];
    word[3u] = w1[3u] ^ w2[3u];
    return word;
}

func write_word_to_flattened_array :: (buf :: [uint8 : AES_EXPANDED_KEY_BYTES]&, idx :: uint32, word :: const WORD&) -> void {
    buf[idx] = word[0u];
    buf[idx+1u] = word[1u];
    buf[idx+2u] = word[2u];
    buf[idx+3u] = word[3u];
}

func aes_key_schedule :: (ctx :: AESCtx&) -> void {
    let N :: const uint32 = 2u * (ctx.mode as uint32) + 4u;
    let R :: const uint32 = 2u * (ctx.mode as uint32) + 11u;

    // Assume AESCtx.key is prefilled with key for the first N words

    for (let i :: uint32 = N; i < 4u * R; i += 1u) {
        if (i >= N && i % N == 0u) {
            // W_{i-N} + SubWord(RotWord(W_{i-1})) + rcon_{i/N}
            let t1 :: WORD = read_word_from_flattened_array(ctx.key, 4u * (i - 1u));
            RotWord(t1);
            SubWord(t1);

            let t0 :: WORD = read_word_from_flattened_array(ctx.key, 4u * (i - N));
            
            t0 = XorWord(t0, t1);
            t0 = XorWord(t0, rcon(i / N));

            write_word_to_flattened_array(ctx.key, 4u * i, t0);
        } else if (i >= N && N > 6u && i % N == 4u) {
            // W_{i-N} + SubWord(W_{i-1})
            let t1 :: WORD = read_word_from_flattened_array(ctx.key, 4u * (i - 1u));
            SubWord(t1);

            let t0 :: WORD = read_word_from_flattened_array(ctx.key, 4u * (i - N));
            
            t0 = XorWord(t0, t1);
            write_word_to_flattened_array(ctx.key, 4u * i, t0);
        } else {
            // W_{i-N} + W_{i-1}
            let t0 :: WORD = read_word_from_flattened_array(ctx.key, 4u * (i - N));

            let t1 :: WORD = read_word_from_flattened_array(ctx.key, 4u * (i - 1u));

            t0 = XorWord(t0, t1);

            write_word_to_flattened_array(ctx.key, 4u * i, t0);
        }
    }
}

func aes_enc :: (ctx :: AESCtx&) -> void {
    // Number of rounds
    let R :: const uint32 = 2u * (ctx.mode as uint32) + 9u;
    
    // Key schedule
    aes_key_schedule(ctx);

    // Add round key
    aes_add_round_key(ctx, 0u);

    for (let i :: uint32 = 0u; i < R; i += 1u) {
        aes_sub_bytes(ctx);
        aes_shift_rows(ctx);
        aes_mix_columns(ctx);
        aes_add_round_key(ctx, i + 1u);
    }

    aes_sub_bytes(ctx);
    aes_shift_rows(ctx);
    aes_add_round_key(ctx, R + 1u);
}

// Adding round key to data
func aes_add_round_key :: (ctx :: AESCtx&, round :: uint32) -> void {
    for (let i :: uint32 = 0u; i < #ctx.block as uint32; i += 1u) {
        ctx.block[i] ^= ctx.key[16u * round + i];
    }
}

func aes_sub_bytes :: (ctx :: AESCtx&) -> void {
    for (let i :: uint32 = 0u; i < #ctx.block as uint32; i += 1u) {
        ctx.block[i] = sbox[ctx.block[i]];
    }
}

func aes_shift_rows :: (ctx :: AESCtx&) -> void {
    let temp :: uint8;

    // Row 0 stays

    // Row 1
    temp = ctx.block[1u];
    ctx.block[1u] = ctx.block[5u];
    ctx.block[5u] = ctx.block[9u];
    ctx.block[9u] = ctx.block[13u];
    ctx.block[13u] = temp;

    // Row 2
    temp = ctx.block[2u];
    ctx.block[2u] = ctx.block[10u];
    ctx.block[10u] = temp;
    temp = ctx.block[6u];
    ctx.block[6u] = ctx.block[14u];
    ctx.block[14u] = temp;

    // Row 3
    temp = ctx.block[15u];
    ctx.block[15u] = ctx.block[11u];
    ctx.block[11u] = ctx.block[7u];
    ctx.block[7u] = ctx.block[3u];
    ctx.block[3u] = temp;
}

func aes_mult_mat :: (word :: WORD&) -> void {
    let a0 :: uint8 = word[0u];
    let a1 :: uint8 = word[1u];
    let a2 :: uint8 = word[2u];
    let a3 :: uint8 = word[3u];
    
    word[0u] = g_table_2[a0] ^ g_table_3[a1] ^ a2 ^ a3;
    word[1u] = a0 ^ g_table_2[a1] ^ g_table_3[a2] ^ a3;
    word[2u] = a0 ^ a1 ^ g_table_2[a2] ^ g_table_3[a3];
    word[3u] = g_table_3[a0] ^ a1 ^ a2 ^ g_table_2[a3];
}

func aes_mix_columns :: (ctx :: AESCtx&) -> void {
    for (let i :: uint32 = 0u; i < #ctx.block / 4u; i += 1u) {
        let col :: WORD = [uint8 : 4u; ctx.block[4u * i], ctx.block[4u * i + 1u], ctx.block[4u * i + 2u], ctx.block[4u * i + 3u]];
        aes_mult_mat(col);

        ctx.block[4u * i] = col[0u];
        ctx.block[4u * i + 1u] = col[1u];
        ctx.block[4u * i + 2u] = col[2u];
        ctx.block[4u * i + 3u] = col[3u];
    }
}
