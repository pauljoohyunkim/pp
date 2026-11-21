
func increment_iv_counter :: (iv_ctr :: IV_COUNTER&) -> void {
    let block_length :: const uint32 = 16u;
    let ctr_length :: const uint32 = 4u;

    for (let i :: uint32 = 0u; i < ctr_length; i += 1u) {
        if (iv_ctr[block_length - 1u - i] < 255uss) {
            iv_ctr[block_length - 1u - i] += 1uss;
            break;
        }
    }
}