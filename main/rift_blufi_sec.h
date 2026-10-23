#pragma once

#include <stdbool.h>
#include <stdint.h>

void rift_blufi_negotiate(uint8_t *data, int len, uint8_t **output_data,
                          int *output_len, bool *need_free);
int rift_blufi_encrypt(uint8_t iv8, uint8_t *data, int len);
int rift_blufi_decrypt(uint8_t iv8, uint8_t *data, int len);
uint16_t rift_blufi_checksum(uint8_t iv8, uint8_t *data, int len);
int rift_blufi_security_init(void);
void rift_blufi_security_deinit(void);
