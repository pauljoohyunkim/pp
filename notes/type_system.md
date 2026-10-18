# PP Language Type System

## Primitive Types
The following is a list of primitive types.
* `int32`
* `float`
* `bool`
* `void`
    * Note that you are not allowed to define a variable void.
The list is expected to grow. Future primitive types to support include:
* `int8`, `int16`, etc.
* `uint8`, `uint16`, etc.
* `int`
    * Arbitrarily large or small integer type.
* `pointer`
    * Not recommended for general programming, but provided for even lower-level management.

## Typedef and Newtype
* typedef is an alias to a type. Internally, it is equivalent to the original type.
* newtype is a separate type based on a primitive. Since source type and this newly created type are different these are not compatible, except during casting.
    * Do not expect to create a type called `INT32` from `int32` via newtype and have them work on addition. You should cast `INT32` to `int32` before doing that.
    * This may require operator overloading mechanism.

## Const and Reference: Type Modifiers
* Const means a variable cannot be modified after declaration.
* Reference for a variable means modifying a reference variable changes the value of the source variable. Ownership still lies with the original variable.
* Reference wraps const. eg: `const int32&` means "ref(const(int32))".

## Function and Lambda
* Function is a regular function in general purpose programming.
* Lambda is a pure state-free function based on functional programming.
No mutable reference passing allowed.
No state allowed within the function body.
In fact, the syntax for defining lambda function is very different.
    * In C++, there is also lambda function. In PP language, this is very different; C++ lambda function is not mathematically correct. I am following the definition of lambda function from lambda calculus.
    * Lambda *CAN* use const global value, as well as reference to const global.
* Function can take any number of different parameters and output something (or nothing, if return type is `void`)
* Lambda can take any number of parameters, but output exactly one thing.

## Struct
* A type for holding many variables as one.
* During declaration, one could give default initializers.
    * If it contains reference as a field, it cannot be given default initializer.
* During instantiation, one could override default initializers.
    * If it contains reference as a field, this must be given.

## Array and List
* Array is a contiguous memory containing value of same type.
The number of values is determined at compile-time.
* List is similar, but size can be changed at run-time.
Expected to be less efficient than array, but easier to use.
* Infinite size is not supported.
* **Array/List element types must be complete default-constructible value types.**
  The general rule for element types:
  - No `Reference`.
  - No `void`.
  - No function or lambda types (for now).
  - No type that transitively contains a reference, e.g. a struct with a reference field or an array/list of such a struct. Use the existing `type_contains_reference` helper.
  - Type must be complete. Array size must be non-zero.
  - Newtype is judged by its underlying type.
  Reference is allowed only as an outer modifier to the whole collection, e.g. `const [int32 : 4u]&` meaning `Ref(Array(4, Const(Int32)))`. The banned form `[const int32& : 4u]` is illegal because the element type is a reference and would require per-element initialization and rebinding semantics.


## Tuple
* A tuple is a type containing multiple values of potentially different types.
Size cannot change.
