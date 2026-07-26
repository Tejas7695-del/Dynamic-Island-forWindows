import ctypes

EnumWindows = ctypes.windll.user32.EnumWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int))
GetWindowText = ctypes.windll.user32.GetWindowTextW
GetWindowTextLength = ctypes.windll.user32.GetWindowTextLengthW
GetClassName = ctypes.windll.user32.GetClassNameW

with open("titles.txt", "w", encoding="utf-8") as f:
    def foreach_window(hwnd, lParam):
        length = GetWindowTextLength(hwnd)
        if length > 0:
            buff = ctypes.create_unicode_buffer(length + 1)
            GetWindowText(hwnd, buff, length + 1)
            
            cls_buff = ctypes.create_unicode_buffer(256)
            GetClassName(hwnd, cls_buff, 256)
            
            f.write(f"[{cls_buff.value}] {buff.value}\n")
        return True

    EnumWindows(EnumWindowsProc(foreach_window), 0)
