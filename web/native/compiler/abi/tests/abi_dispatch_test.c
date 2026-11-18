#include "abi_cases.h"
#include "khdays_references.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(sizeof(void *) == 4 && sizeof(long) == 4, "wasm32 pointer/long ABI");
_Static_assert(sizeof(long long) == 8 && sizeof(double) == 8, "64-bit scalar ABI");
_Static_assert(sizeof(long double) == 16, "long double needs a separate DS-layout audit");
_Static_assert(_Alignof(long long) == 8, "scalar natural alignment");
_Static_assert(_Alignof(struct R8Single) == 4, "emitted game record packing");
_Static_assert(sizeof(struct R1) == 1 && sizeof(struct R2) == 2 && sizeof(struct R4) == 4, "small records");
_Static_assert(sizeof(struct R4Multi) == 4 && sizeof(struct R8) == 8 && sizeof(struct R12) == 12, "multi records");

/* The harness supplies a small explicit table of the actual emitter's records.
 * This tests the rewrite's function-reference ABI, not a production registry. */
#include "fixture_record_lookup.h"

extern struct R8 (*khdays_route_make8)(int);
static struct R8 changed8(int x) { struct R8 result = {x + 100, x + 200}; return result; }

int main(void)
{
    assert(make1(7).a == 7 && via1(8).a == 8);
    assert(make2(70).a == 70 && via2(80).a == 80);
    assert(make4(700).a == 700 && via4(800).a == 800);
    struct R4Multi r4m = via4multi(9);
    assert(r4m.a == 9 && r4m.b == 10 && r4m.c == 11 && r4m.d == 12);
    struct R8 r8 = via8(17);
    assert(r8.a == 17 && r8.b == 18);
    assert(make8single(3).a == 0x100000003ll);
    assert(via8single(5).a == 0x100000005ll);
    struct R12 r12 = via12(20);
    assert(r12.a == 20 && r12.b == 21 && r12.c == 22);
    assert(makefloat(3).a == 3.5f && viafloat(5).a == 5.5f);
    assert(makedouble(3).a == 3.25 && viadouble(5).a == 5.25);
    assert(makenested(100).a[0].a == 100 && vianested(101).a[0].a == 101);
    struct R8 (*through_record)(int) = (struct R8 (*)(int))KHDAYS_FN(KHDAYS_FREF(make8));
    assert(through_record(11).a == 11);
    struct R8 (*previous)(int) = khdays_route_make8;
    khdays_route_make8 = changed8;
    assert(make8(9).a == 109 && via8(9).b == 209);
    assert(through_record(11).a == 111);
    assert(previous(9).b == 10);
    khdays_route_make8 = previous;
    assert(via8(9).a == 9 && through_record(11).a == 11);
    puts("actual rewritten two-TU Wasm aggregate returns and typed record dispatch: passed");
    return 0;
}
