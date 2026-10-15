#ifndef MTW_BATTLE_ICON_ATLAS_FIX_H
#define MTW_BATTLE_ICON_ATLAS_FIX_H

/* Installs the verified Medieval PE32 atlas reset hook at most once per
 * process. Returns zero without changing game code when signatures differ. */
int mtw_battle_icon_atlas_install_once(void);

#endif
