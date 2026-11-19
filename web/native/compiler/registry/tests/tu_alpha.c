#include "static_registry.h"
#include "decomp_compat.h"
#include "fixture_memory.h"

static const unsigned alpha_image = 0x12345678u;
/* Names intentionally oppose declaration/source order. Do not alphabetize data. */
__declspec(allocate(".khdi$m")) struct khdays_data_init khdays_place_z_first = {FIXTURE_MEMORY_BASE + 16u, &alpha_image, sizeof(alpha_image), -4};

static int alpha_body(int value) { return value + 10; }
int (*khdays_route_alpha)(int) = alpha_body;
int alpha(int value) { return khdays_route_alpha(value); }
__declspec(allocate(".khrt$m")) const struct khdays_route_record khdays_route_record_alpha = {
    "alpha", (void **)&khdays_route_alpha, (void *)alpha_body, 0
};
__declspec(allocate(".khfr$m")) const struct khdays_function_record khdays_function_alpha = {0xfe00000cu, (void (*)(void))alpha};
int beta(int value);
/* The same numeric reference legitimately occurs in another TU too. */
__declspec(allocate(".khfr$m")) const struct khdays_function_record khdays_function_beta_again = {0xfe000004u, (void (*)(void))beta};

/* Decoys must not enter the registry:
const struct khdays_function_record fake_comment = {0}; */
const char *decoy = "const struct khdays_route_record fake_string = {0};";
