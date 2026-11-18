/* Exercise the header used by the generated game C in its own C unit. */
#include "compat/khdays_references.h"

void *generated_address(khdays_ref reference)
{
    return KHDAYS_W(reference);
}

void *generated_host(khdays_ref reference)
{
    return khdays_host(reference);
}

khdays_ref generated_reference(const void *host)
{
    return KHDAYS_REF(host);
}
