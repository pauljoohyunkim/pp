lambda fibo :: (x :: uint32) -> uint32 {
    if (x == 0u || x == 1u) {
        return 1u;
    } else {
        return fibo(x - 1u) + fibo(x - 2u);
    }
}
