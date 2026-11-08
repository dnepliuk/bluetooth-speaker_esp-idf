#include "rotary_steps.h"

constexpr bool clockwiseAndBack() {
    RotarySteps steps(2);
    return steps.take(1) == 0 && steps.take(2) == 1 && steps.take(4) == 1 &&
           steps.take(3) == 0 && steps.take(2) == -1 && steps.take(0) == -1;
}
constexpr bool reversalCancelsHalfDetent() {
    RotarySteps steps(2);
    return steps.take(1) == 0 && steps.take(0) == 0 && steps.take(-1) == 0 &&
           steps.take(-2) == -1 && steps.take(-1) == 0 && steps.take(0) == 1;
}
constexpr bool batchedMovementRetainsRemainder() {
    RotarySteps steps(2);
    return steps.take(7) == 3 && steps.take(8) == 1 && steps.take(-5) == -6 &&
           steps.take(-6) == -1;
}
constexpr bool reversedWiring() {
    RotarySteps steps(2, true);
    return steps.take(2) == -1 && steps.take(0) == 1;
}
constexpr bool counterWrapDoesNotJump() {
    RotarySteps steps(2);
    steps.take(INT32_MAX - 1);
    return steps.take(INT32_MIN) == 1 && steps.take(INT32_MAX - 1) == -1;
}
static_assert(clockwiseAndBack());
static_assert(reversalCancelsHalfDetent());
static_assert(batchedMovementRetainsRemainder());
static_assert(reversedWiring());
static_assert(counterWrapDoesNotJump());
