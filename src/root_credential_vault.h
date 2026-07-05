/*
 * root_credential_vault.h — Encrypted root password vault for taskmgr.
 *
 * Validates via sudo -S -k true, stores password XOR-encrypted in mlocked RAM
 * until root_mode_deactivate() or process exit.
 */

#ifndef ROOT_CREDENTIAL_VAULT_H
#define ROOT_CREDENTIAL_VAULT_H

#include <stddef.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROOT_VAULT_MAX_PASS 256

typedef enum {
    ROOT_ELEVATION_NONE = 0,
    ROOT_ELEVATION_FULL = 1
    /* ROOT_ELEVATION_EPHEMERAL — reserved for one-shot elevation (step 2) */
} RootElevationKind;

gboolean root_mode_is_active(void);
RootElevationKind root_mode_kind(void);

gboolean root_mode_activate_with_password(const char *password,
                                          char *errmsg, size_t errmsg_len);
void root_mode_deactivate(void);

gboolean root_validate_password(const char *password,
                                char *errmsg, size_t errmsg_len);

/* Borrow decrypted password; caller MUST secure_zero the buffer after use. */
gboolean root_vault_borrow_password(char *out, size_t out_size);

void root_vault_secure_zero(void *ptr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ROOT_CREDENTIAL_VAULT_H */
