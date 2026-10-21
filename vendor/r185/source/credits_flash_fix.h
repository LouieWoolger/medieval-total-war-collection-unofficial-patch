#ifndef MTW_CREDITS_FLASH_FIX_H
#define MTW_CREDITS_FLASH_FIX_H

/* Installs the credits-only game hooks after a D3D9 factory succeeds.
 * Returns one only when both exact call sites have been patched. */
int mtw_credits_flash_install_once(void);

#endif
