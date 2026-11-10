extern func pp_std_print_string :: (str :: string) -> void;
extern func pp_std_print_float :: (val :: float) -> void;

struct Mat2 {
    r0 :: [float : 2u];
    r1 :: [float : 2u];
}

struct Vec2 {
    x :: float;
    y :: float;
}

operator * :: (M :: const Mat2&, v :: const Vec2&) -> Vec2 {
    return Vec2 { .x = M.r0[0u] * v.x + M.r0[1u] * v.y,
                  .y = M.r1[0u] * v.x + M.r1[1u] * v.y };
}

operator / :: (v :: const Vec2&, s :: const float&) -> Vec2 {
    return Vec2 { .x = v.x / s, .y = v.y / s };
}

func print_mat2 :: (M :: const Mat2&) -> void {
    pp_std_print_float(M.r0[0u]); pp_std_print_string(", "); pp_std_print_float(M.r0[1u]); pp_std_print_string("\n");
    pp_std_print_float(M.r1[0u]); pp_std_print_string(", "); pp_std_print_float(M.r1[1u]); pp_std_print_string("\n");
}

func print_vec2 :: (v :: const Vec2&) -> void {
    pp_std_print_float(v.x); pp_std_print_string("\n"); pp_std_print_float(v.y); pp_std_print_string("\n");
}

lambda norm_vec2 :: (v :: const Vec2&) -> float {
    let s :: const float = v.x * v.x + v.y * v.y;
    let x :: float = s / 2.0;

    // Square root
    for (let i :: uint32 = 0u; i < 1000u; i += 1u) {
        x = x - (x * x - s) / (2.0 * x);
    }
    return x;
}

func main :: () -> int32 {
    // Matrix to find eigenvector to.
    let M :: const Mat2 = Mat2 {
        .r0 = [float : 2u; 0.0, -6.0],
        .r1 = [float : 2u; 1.0, 5.0]
    };

    pp_std_print_string("Matrix M =\n");
    print_mat2(M);
    pp_std_print_string("\n");

    // Initial Guess vector
    let v :: Vec2 = Vec2 {
        .x = 1.0,
        .y = 0.0
    };
    pp_std_print_string("Initial guess vector v0 =\n");
    print_vec2(v);

    pp_std_print_string("\nUsing power method to find an eigenvector\n");

    for (let i :: uint32 = 0u; i < 100u; i += 1u) {
        v = M * v;
        v = v / norm_vec2(v);
    }

    pp_std_print_string("Found eigenvector:\n");
    print_vec2(v);
    pp_std_print_string("\nFor reference, two linearly independent eigenvectors of M are (-2,1) and (-3,1).\n");
    pp_std_print_string("This program numerically found the former eigenvector.\n");

    return 0;
}
