# SPDX-License-Identifier: GPL-3.0-or-later
# Link-only stubs. The PS5 native loader resolves these public API imports.
.text
.global sceCommonDialogInitialize
.type sceCommonDialogInitialize, @function
sceCommonDialogInitialize:
  ret
.size sceCommonDialogInitialize, .-sceCommonDialogInitialize
