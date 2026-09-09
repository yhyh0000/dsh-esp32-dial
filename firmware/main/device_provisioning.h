#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void device_provisioning_start_http(void);
void device_provisioning_start_ap(void);
bool device_provisioning_ap_active(void);

#ifdef __cplusplus
}
#endif
