/** @file
  HandleProtocolg.c
  v9 UI 增強版：
  1. 新增「GUID 填空輸入模式」 (仿照您提供的截圖)
  2. 輸入時自動跳過 '-'，並限制只能輸入 Hex
  3. 包含 v8 的所有修正 (DEL 功能、輸入緩衝區修正、搜尋功能)
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PrintLib.h>

// Protocol GUIDs
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/PciIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SerialIo.h>
#include <Protocol/SimpleTextOut.h>

// -----------------------------------------------------------
// 全域變數
// -----------------------------------------------------------
EFI_FILE_PROTOCOL *gLogFile = NULL;

typedef struct {
  CHAR16    *Name;
  EFI_GUID  *Guid;
} PROTOCOL_MAP_ENTRY;

PROTOCOL_MAP_ENTRY mProtocolDatabase[] = {
  { L"DevicePath",       &gEfiDevicePathProtocolGuid },
  { L"LoadedImage",      &gEfiLoadedImageProtocolGuid },
  { L"BlockIO",          &gEfiBlockIoProtocolGuid },
  { L"FileSystem",       &gEfiSimpleFileSystemProtocolGuid },
  { L"DriverBinding",    &gEfiDriverBindingProtocolGuid },
  { L"PciIO",            &gEfiPciIoProtocolGuid },
  { L"GraphicsOutput",   &gEfiGraphicsOutputProtocolGuid },
  { L"SerialIO",         &gEfiSerialIoProtocolGuid },
  { NULL, NULL }
};

CHAR16 *mMenuItems[] = {
  L"1. Dump All Handles",
  L"2. Search by Protocol GUID",
  L"3. Search by Protocol Name (Exact Match)",
  L"4. Search by Handle Number (Index)",
  L"Exit"
};
#define MENU_ITEM_COUNT (sizeof(mMenuItems) / sizeof(CHAR16*))

// -----------------------------------------------------------
// Log 系統
// -----------------------------------------------------------
VOID OpenLogFile(IN EFI_HANDLE ImageHandle) {
  EFI_STATUS Status;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL *Root;

  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
  if (EFI_ERROR(Status)) return;
  
  Status = gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&FileSystem);
  if (EFI_ERROR(Status)) return;

  FileSystem->OpenVolume(FileSystem, &Root);
  Root->Open(Root, &gLogFile, L"HandleDump.log", EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
}

VOID CloseLogFile() {
  if (gLogFile) {
    gLogFile->Flush(gLogFile);
    gLogFile->Close(gLogFile);
    gLogFile = NULL;
  }
}

VOID EFIAPI Log(IN CONST CHAR16 *Format, ...) {
  VA_LIST Marker;
  CHAR16 Buffer[1024];
  UINTN Len, i;
  CHAR8 *AsciiBuffer;
  UINTN AsciiLen;

  VA_START(Marker, Format);
  UnicodeVSPrint(Buffer, sizeof(Buffer), Format, Marker);
  VA_END(Marker);

  gST->ConOut->OutputString(gST->ConOut, Buffer);

  if (gLogFile) {
    Len = StrLen(Buffer);
    AsciiBuffer = AllocatePool(Len + 1);
    if (AsciiBuffer) {
      for (i = 0; i < Len; i++) AsciiBuffer[i] = (CHAR8)(Buffer[i] & 0xFF);
      AsciiBuffer[Len] = '\0';
      AsciiLen = Len;
      gLogFile->Write(gLogFile, &AsciiLen, AsciiBuffer);
      FreePool(AsciiBuffer);
    }
  }
}

// -----------------------------------------------------------
// 工具函數
// -----------------------------------------------------------
VOID SetColor(UINTN Attribute) {
  gST->ConOut->SetAttribute(gST->ConOut, Attribute);
}

VOID ResetColor() {
  SetColor(EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
}

UINT8 HexCharToInt(CHAR16 Char) {
  if (Char >= L'0' && Char <= L'9') return (UINT8)(Char - L'0');
  if (Char >= L'A' && Char <= L'F') return (UINT8)(Char - L'A' + 10);
  if (Char >= L'a' && Char <= L'f') return (UINT8)(Char - L'a' + 10);
  return 0;
}

UINTN MyStrToUintn(IN CHAR16 *Str) {
  UINTN Value = 0;
  CHAR16 *Ptr = Str;
  BOOLEAN IsHex = FALSE;

  while (*Ptr == L' ') Ptr++; 
  if (*Ptr == L'\0') return 0;

  if (StrLen(Ptr) > 2 && Ptr[0] == L'0' && (Ptr[1] == L'x' || Ptr[1] == L'X')) {
    IsHex = TRUE;
    Ptr += 2;
  } else {
    CHAR16 *CheckPtr = Ptr;
    while (*CheckPtr != L'\0') {
      if ((*CheckPtr >= L'A' && *CheckPtr <= L'F') || (*CheckPtr >= L'a' && *CheckPtr <= L'f')) {
        IsHex = TRUE; break;
      }
      CheckPtr++;
    }
  }

  while (*Ptr != L'\0') {
    UINTN Digit = 0;
    CHAR16 C = *Ptr;
    if (C >= L'0' && C <= L'9') Digit = C - L'0';
    else if (IsHex && C >= L'A' && C <= L'F') Digit = 10 + (C - L'A');
    else if (IsHex && C >= L'a' && C <= L'f') Digit = 10 + (C - L'a');
    else break; 

    if (IsHex) Value = (Value * 16) + Digit;
    else Value = (Value * 10) + Digit;
    Ptr++;
  }
  return Value;
}

EFI_STATUS StringToGuid(IN CHAR16 *Str, OUT EFI_GUID *Guid) {
  UINTN i;
  if (StrLen(Str) != 36) return EFI_INVALID_PARAMETER;
  Guid->Data1 = 0;
  for (i = 0; i < 8; i++) Guid->Data1 = (Guid->Data1 << 4) | HexCharToInt(Str[i]);
  Guid->Data2 = 0;
  for (i = 9; i < 13; i++) Guid->Data2 = (Guid->Data2 << 4) | HexCharToInt(Str[i]);
  Guid->Data3 = 0;
  for (i = 14; i < 18; i++) Guid->Data3 = (Guid->Data3 << 4) | HexCharToInt(Str[i]);
  Guid->Data4[0] = (HexCharToInt(Str[19]) << 4) | HexCharToInt(Str[20]);
  Guid->Data4[1] = (HexCharToInt(Str[21]) << 4) | HexCharToInt(Str[22]);
  for (i = 0; i < 6; i++) {
    Guid->Data4[2+i] = (HexCharToInt(Str[24 + i*2]) << 4) | HexCharToInt(Str[25 + i*2]);
  }
  return EFI_SUCCESS;
}

// 通用字串輸入 (用於 Name 與 Index)
VOID GetInput(IN CHAR16 *Prompt, OUT CHAR16 *Buffer, IN UINTN BufferSize) {
  EFI_INPUT_KEY Key;
  EFI_STATUS    Status;
  UINTN         Count = 0;
  UINTN         EventIndex = 0; 
  ZeroMem(Buffer, BufferSize * sizeof(CHAR16));
  ResetColor();
  Print(L"%s", Prompt);
  while (TRUE) {
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &EventIndex);
    Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    if (EFI_ERROR(Status)) continue;
    if (Key.UnicodeChar == L'\r') { Print(L"\n"); break; }
    if (Key.UnicodeChar == 0x08) { 
      if (Count > 0) { Print(L"\b \b"); Count--; Buffer[Count] = L'\0'; } 
    }
    else if (Key.UnicodeChar >= 0x20 && Key.UnicodeChar <= 0x7E) {
      if (Count < BufferSize - 1) { Print(L"%c", Key.UnicodeChar); Buffer[Count++] = Key.UnicodeChar; }
    }
  }
  Buffer[Count] = L'\0';
}

// -----------------------------------------------------------
// ★★★ 新功能: GetGuidInput (專門處理 GUID 填空介面) ★★★
// -----------------------------------------------------------
VOID GetGuidInput(OUT CHAR16 *Buffer) {
  EFI_INPUT_KEY Key;
  UINTN         Count = 0;
  UINTN         EventIndex = 0;
  // 預設樣板: 36 個字元 + Null
  CHAR16        GuidStr[37] = L"________-____-____-____-____________"; 
  UINTN         i;

  ResetColor();
  Print(L"GUID: ");
  Print(L"%s", GuidStr); // 印出樣板

  // 將游標移回開頭 (左移 36 格)
  for(i=0; i<36; i++) Print(L"\b");

  while (TRUE) {
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &EventIndex);
    if (EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &Key))) continue;

    // Enter 確認
    if (Key.UnicodeChar == L'\r') {
      // 檢查是否填滿 (簡單檢查是否有 '_')
      BOOLEAN Complete = TRUE;
      for (i=0; i<36; i++) {
        if (GuidStr[i] == L'_') Complete = FALSE;
      }
      
      if (Complete) {
        Print(L"\n");
        break; 
      }
      // 如果沒填完，不動作 (或者可以嗶一聲)
    }
    // Backspace 處理
    else if (Key.UnicodeChar == 0x08) {
      if (Count > 0) {
        Count--;
        // 如果倒退遇到 '-'，再多退一格
        if (Count == 8 || Count == 13 || Count == 18 || Count == 23) {
          Print(L"\b"); // 游標過 '-'
          Count--;
        }
        
        GuidStr[Count] = L'_'; // 恢復底線
        Print(L"\b_\b");       // 視覺上：倒退、印底線、再倒退回該格
      }
    }
    // Hex 輸入處理 (0-9, a-f, A-F)
    else if ((Key.UnicodeChar >= L'0' && Key.UnicodeChar <= L'9') ||
             (Key.UnicodeChar >= L'a' && Key.UnicodeChar <= L'f') ||
             (Key.UnicodeChar >= L'A' && Key.UnicodeChar <= L'F')) {
               
      if (Count < 36) {
        // 轉大寫顯示比較好看
        CHAR16 Char = Key.UnicodeChar;
        if (Char >= L'a' && Char <= L'f') Char -= (L'a' - L'A');

        GuidStr[Count] = Char;
        Print(L"%c", Char); // 印出字元
        Count++;

        // 如果打完字剛好遇到 '-'，自動跳過
        if (Count == 8 || Count == 13 || Count == 18 || Count == 23) {
          GuidStr[Count] = L'-'; // 確保 Buffer 裡是 '-'
          Print(L"-");           // 螢幕上印 '-' (游標前進)
          Count++;
        }
      }
    }
  }
  
  // 複製結果出去
  StrCpyS(Buffer, 100, GuidStr);
}

CHAR16 MyCharToUpper(CHAR16 C) {
  if (C >= L'a' && C <= L'z') return C - (L'a' - L'A');
  return C;
}

INTN MyStrCmpi(IN CHAR16 *Str1, IN CHAR16 *Str2) {
  while (*Str1 != L'\0' && *Str2 != L'\0') {
    if (MyCharToUpper(*Str1) != MyCharToUpper(*Str2)) return 1;
    Str1++; Str2++;
  }
  if (*Str1 == L'\0' && *Str2 == L'\0') return 0; 
  return 1;
}

CHAR16* GetProtocolName(IN EFI_GUID *Guid) {
  UINTN i = 0;
  while (mProtocolDatabase[i].Name != NULL) {
    if (CompareGuid(Guid, mProtocolDatabase[i].Guid)) return mProtocolDatabase[i].Name;
    i++;
  }
  return L"Unknown GUID";
}

EFI_GUID* GetGuidByName(IN CHAR16 *Name) {
  UINTN i = 0;
  while (mProtocolDatabase[i].Name != NULL) {
    if (MyStrCmpi(mProtocolDatabase[i].Name, Name) == 0) return mProtocolDatabase[i].Guid;
    i++;
  }
  return NULL;
}

// -----------------------------------------------------------
// 顯示邏輯
// -----------------------------------------------------------
VOID DumpHandleFormat(IN EFI_HANDLE Handle, IN UINTN Index) {
  EFI_STATUS Status;
  EFI_GUID **PList;
  UINTN PCount, i;
  EFI_DEVICE_PATH_PROTOCOL *Dp;
  CHAR16 *DpStr;
  VOID *Interface;

  Log(L"----------------------------------------------------------\n");
  Log(L"Handle Index: %d  |  Address: 0x%p\n", Index, Handle);

  Status = gBS->HandleProtocol(Handle, &gEfiDevicePathProtocolGuid, (VOID**)&Dp);
  if (!EFI_ERROR(Status)) {
    DpStr = ConvertDevicePathToText(Dp, TRUE, TRUE);
    if (DpStr) { Log(L"  Device Path: %s\n", DpStr); FreePool(DpStr); }
  } else {
    Log(L"  Device Path: [None]\n");
  }

  Status = gBS->ProtocolsPerHandle(Handle, &PList, &PCount);
  if (!EFI_ERROR(Status) && PList != NULL) {
    Log(L"  Protocols (%d):\n", PCount);
    for (i = 0; i < PCount; i++) {
      Status = gBS->HandleProtocol(Handle, PList[i], &Interface);
      if (!EFI_ERROR(Status)) {
        Log(L"    - %g (%s) -> Interface: 0x%p\n", PList[i], GetProtocolName(PList[i]), Interface);
      } else {
        Log(L"    - %g (%s)\n", PList[i], GetProtocolName(PList[i]));
      }
    }
    FreePool(PList);
  }
}

VOID SearchByProtocol(IN EFI_GUID *TargetGuid, IN BOOLEAN PrintDetail) {
  EFI_STATUS Status;
  UINTN Count, i, k, PCount;
  EFI_HANDLE *Buffer;
  EFI_GUID **PList;
  BOOLEAN Found = FALSE;

  gBS->LocateHandleBuffer(AllHandles, NULL, NULL, &Count, &Buffer);
  for (i = 0; i < Count; i++) {
    Status = gBS->ProtocolsPerHandle(Buffer[i], &PList, &PCount);
    if (!EFI_ERROR(Status)) {
      for (k = 0; k < PCount; k++) {
        if (CompareGuid(PList[k], TargetGuid)) {
          if (PrintDetail) {
             DumpHandleFormat(Buffer[i], i);
          } else {
             Log(L"Found at Index: %d (Address: 0x%p) -> Match\n", i, Buffer[i]);
          }
          Found = TRUE;
          break;
        }
      }
      FreePool(PList);
    }
  }
  if (!Found) Log(L"No handles found.\n");
  if (Buffer) FreePool(Buffer);
}

// -----------------------------------------------------------
// UI
// -----------------------------------------------------------
UINTN RunMenu() {
  EFI_INPUT_KEY Key;
  UINTN Index;
  UINTN Selection = 0;
  gST->ConOut->EnableCursor(gST->ConOut, FALSE);
  while (1) {
    gST->ConOut->ClearScreen(gST->ConOut);
    ResetColor();
    Print(L"=== UEFI Handle Utility ===\n");
    Print(L"UP/DOWN to select, ENTER to confirm\n\n");
    for (Index = 0; Index < MENU_ITEM_COUNT; Index++) {
      if (Index == Selection) {
        SetColor(EFI_TEXT_ATTR(EFI_WHITE, EFI_BLUE));
        Print(L" -> %s \n", mMenuItems[Index]);
      } else {
        SetColor(EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
        Print(L"    %s \n", mMenuItems[Index]);
      }
    }
    ResetColor();
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
    gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    if (Key.ScanCode == SCAN_UP) { if (Selection > 0) Selection--; else Selection = MENU_ITEM_COUNT - 1; }
    else if (Key.ScanCode == SCAN_DOWN) { if (Selection < MENU_ITEM_COUNT - 1) Selection++; else Selection = 0; }
    else if (Key.UnicodeChar == L'\r') { gST->ConOut->EnableCursor(gST->ConOut, TRUE); gST->ConOut->ClearScreen(gST->ConOut); return Selection; }
  }
}

// -----------------------------------------------------------
// 主程式
// -----------------------------------------------------------
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  UINTN Selection;
  CHAR16 InputBuffer[100];
  UINTN HandleCount;
  EFI_HANDLE *HandleBuffer;
  UINTN TargetIndex;
  EFI_GUID TargetGuid;
  EFI_GUID *FoundGuidPtr;
  UINTN Dummy;
  EFI_INPUT_KEY DummyKey;

  OpenLogFile(ImageHandle);
  
  while (TRUE) {
    Selection = RunMenu();
    if (Selection == 4) break; 

    Log(L"--- Execution Result ---\n");

    switch (Selection) {
    case 0: // Dump All
      gBS->LocateHandleBuffer(AllHandles, NULL, NULL, &HandleCount, &HandleBuffer);
      Log(L"Total Handles: %d\n", HandleCount);
      for (UINTN i = 0; i < HandleCount; i++) DumpHandleFormat(HandleBuffer[i], i);
      FreePool(HandleBuffer);
      break;

    case 1: // GUID Search
      // ★★★ 使用新的 GetGuidInput 函數 ★★★
      GetGuidInput(InputBuffer); 
      
      if (!EFI_ERROR(StringToGuid(InputBuffer, &TargetGuid))) {
        SearchByProtocol(&TargetGuid, TRUE);
      } else {
        Log(L"Invalid GUID!\n");
      }
      break;

    case 2: // Name Search
      Log(L"Supported: DevicePath, BlockIO, FileSystem, PciIO, SerialIO...\n");
      GetInput(L"Input EXACT Name: ", InputBuffer, 100);
      
      FoundGuidPtr = GetGuidByName(InputBuffer);
      if (FoundGuidPtr) {
        Log(L"[Debug] Found Protocol: %s (%g)\n", GetProtocolName(FoundGuidPtr), FoundGuidPtr);
        SearchByProtocol(FoundGuidPtr, FALSE);
      } else {
        Log(L"Name '%s' not found.\n", InputBuffer);
      }
      break;

    case 3: // Index Search
      GetInput(L"Input Handle Index (e.g. 10 or 0xA): ", InputBuffer, 100);
      TargetIndex = MyStrToUintn(InputBuffer);
      
      Log(L"[Debug] Parsed Index: %d (0x%X)\n", TargetIndex, TargetIndex);

      gBS->LocateHandleBuffer(AllHandles, NULL, NULL, &HandleCount, &HandleBuffer);
      if (TargetIndex < HandleCount) {
        DumpHandleFormat(HandleBuffer[TargetIndex], TargetIndex);
      } else {
        Log(L"Index %d is out of range (Max: %d)\n", TargetIndex, HandleCount - 1);
      }
      FreePool(HandleBuffer);
      break;
    }

    Log(L"\nPress any key to return to menu...");
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Dummy);
    gST->ConIn->ReadKeyStroke(gST->ConIn, &DummyKey);
  }

  CloseLogFile();
  return EFI_SUCCESS;
}