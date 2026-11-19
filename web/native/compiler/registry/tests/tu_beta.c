#include "static_registry.h"
#include "decomp_compat.h"
#include "fixture_memory.h"

static const unsigned alias_image = 0xaabbccddu;
__declspec(allocate(".khdi$m")) struct khdays_data_init khdays_place_a_second = {FIXTURE_MEMORY_BASE + 16u, &alias_image, sizeof(alias_image), -4};
static const unsigned beta_image = 0x87654321u;
__declspec(allocate(".khdi$m")) struct khdays_data_init khdays_place_beta = {FIXTURE_MEMORY_BASE + 32u, &beta_image, sizeof(beta_image), 7};

static int beta_body(int value) { return value * 3; }
int (*khdays_route_beta)(int) = beta_body;
int beta(int value) { return khdays_route_beta(value); }
__declspec(allocate(".khrt$m")) const struct khdays_route_record khdays_route_record_beta = {
    "beta", (void **)&khdays_route_beta, (void *)beta_body, 0
};
__declspec(allocate(".khfr$m")) const struct khdays_function_record khdays_function_beta = {0xfe000004u, (void (*)(void))beta};
