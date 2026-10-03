struct Vector {
    x :: int32;
    y :: int32&; // Fail
}
let x :: const int32 = 2;
let y :: Vector;
