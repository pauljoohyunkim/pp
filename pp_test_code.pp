struct Data { a :: int32; b :: int32; c :: int32; }
func test_multi :: (d :: Data) -> int32 { let sum :: int32 = d.a + d.b + d.c; return sum; }
