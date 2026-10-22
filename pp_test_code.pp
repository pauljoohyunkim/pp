func relu :: (x :: float) -> float { return 1.0; }
// func test_void :: () -> void { return; }
let global_x :: float = 1.0;
func test_ref :: () -> float& { return global_x; }
func test_void :: () -> void { return; }
