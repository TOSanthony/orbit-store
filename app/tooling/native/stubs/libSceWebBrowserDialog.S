# SPDX-License-Identifier: GPL-3.0-or-later
# Link-only stubs. The PS5 native loader resolves these public API imports.
.text
.global sceWebBrowserDialogInitialize
.type sceWebBrowserDialogInitialize, @function
sceWebBrowserDialogInitialize:
  ret
.size sceWebBrowserDialogInitialize, .-sceWebBrowserDialogInitialize
.global sceWebBrowserDialogOpen
.type sceWebBrowserDialogOpen, @function
sceWebBrowserDialogOpen:
  ret
.size sceWebBrowserDialogOpen, .-sceWebBrowserDialogOpen
.global sceWebBrowserDialogUpdateStatus
.type sceWebBrowserDialogUpdateStatus, @function
sceWebBrowserDialogUpdateStatus:
  ret
.size sceWebBrowserDialogUpdateStatus, .-sceWebBrowserDialogUpdateStatus
.global sceWebBrowserDialogGetResult
.type sceWebBrowserDialogGetResult, @function
sceWebBrowserDialogGetResult:
  ret
.size sceWebBrowserDialogGetResult, .-sceWebBrowserDialogGetResult
.global sceWebBrowserDialogClose
.type sceWebBrowserDialogClose, @function
sceWebBrowserDialogClose:
  ret
.size sceWebBrowserDialogClose, .-sceWebBrowserDialogClose
