#Persistent
#SingleInstance, Force
; Script AutoHotkey v1 untuk membuka On-Screen Keyboard saat F24 ditekan.
; F24 dikirim oleh ds4gopher.exe saat tombol Triangle/Y ditekan.

F24::
    Run, osk.exe
return
