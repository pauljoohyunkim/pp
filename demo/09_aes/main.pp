
enum ProgMode :: uint32 {
    Encryption,
    Decryption
}

func main :: (argc :: int32, argv :: ptr[ptr[uint8]]) -> int32 {
    let args :: [string] = pp_get_argv(argc, argv);
    
    if (argc as uint32 != 5u) {
        pp_print_string("Usage: ");
        pp_print_string(args[0u]);
        pp_print_string(" [e/d] <input file> <key file> <output file>\n");
        return 1;
    }

    let prog_mode :: ProgMode;
    if (args[1u] == "e") {
        prog_mode = ProgMode::Encryption;
    } else if (args[1u] == "d") {
        prog_mode = ProgMode::Decryption;
    } else {
        pp_print_string("The first argument must either be \"e\" or \"d\".\n");
        return 1;
    }

    let input_filename :: const string& = args[2u];
    let key_filename :: const string& = args[3u];
    let output_filename :: const string& = args[4u];

    // Read key file.
    let key :: [uint8 : 15u * 4u * 4u];
    let aes_mode :: AESMode;
    {
        let keyfile :: PP_File = pp_file_open(key_filename, PP_File_OpenMode::PP_FOPEN_READ_ONLY);
        let read_bytes :: uint32 = pp_file_read(keyfile, @key[0u], 32ul) as uint32;
        if (read_bytes == 16u) {
            aes_mode = AESMode::AES128;
        } else if (read_bytes == 24u) {
            aes_mode = AESMode::AES192;
        } else if (read_bytes == 32u) {
            aes_mode = AESMode::AES256;
        } else {
            pp_print_string("[-] Key file must be either 16, 24, or 32 bytes exactly\n");
            return 1;
        }
    }

    // Read input and output
    let input_file :: PP_File = pp_file_open(input_filename, PP_File_OpenMode::PP_FOPEN_READ_ONLY);

    // Get secure nonce + counter
    let iv_ctr :: [uint8 : 16u];
    let output_file :: PP_File = pp_file_open(output_filename, PP_File_OpenMode::PP_FOPEN_WRITE_ONLY);
    if (prog_mode == ProgMode::Encryption) {
        rand_bytes(@iv_ctr as uint64 as ptr[uint8], 12u);
        pp_file_write(output_file, @iv_ctr[0u] as ptr[const uint8], 12ul);
    } else {
        let read_bytes :: uint32 = pp_file_read(input_file, @iv_ctr[0u], 12ul) as uint32;
        if (read_bytes < 12u) {
            pp_print_string("You asked for encryption, but the ciphertext file does not contain IV.\n");
            return 1;
        }
    }


    // Instantiate AES-CTR context
    let ctx :: AESCtx = AESCtx {
        .mode = aes_mode,
        .key = key,
        .block = iv_ctr
    };
    while (true) {
        let data_buffer :: [uint8 : 16u];
        let read_bytes :: uint32 = pp_file_read(input_file, @data_buffer[0u], #ctx.block as uint64) as uint32;
        if (read_bytes == 0u) {
            break;
        }

        // Compute encryption of IV+Counter block
        aes_enc(ctx);

        // XOR data with encrypted IV || Counter
        xor16(data_buffer, ctx.block);

        // Write the data chunk + encr(IV || Counter)
        let write_bytes :: uint32 = pp_file_write(output_file, @data_buffer[0u] as ptr[const uint8], read_bytes as uint64) as uint32;

        // Increment counter
        increment_iv_counter(iv_ctr);
        ctx.block = iv_ctr;
    }

    return 0;
}

func xor16 :: (buf1 :: [uint8 : 16u]&, buf2 :: const [uint8 : 16u]&) -> void {
    for (let i :: uint32 = 0u ; i < #buf1; i += 1u) {
        buf1[i] ^= buf2[i];
    }
}