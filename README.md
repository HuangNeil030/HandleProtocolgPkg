# HandleProtocolgPkg
UEFI Handle Inspector (HandleProtocolg) 技術文件
1. 專案概述
本工具是一個在 UEFI Shell 下執行的診斷程式，用於遍歷、搜尋與分析系統中的 Handle 資料庫。 主要解決了 EDK2 標準庫在輸入解析上的局限性，並提供了圖形化選單 (TUI) 與自動 Log 功能。

2. 核心函數庫 (API Reference)
2.1 輸入處理系統 (Input Handling)
VOID GetInput(IN CHAR16 *Prompt, OUT CHAR16 *Buffer, IN UINTN BufferSize)
功能：通用的文字輸入介面，支援 Backspace 修改。

參數：

Prompt: 顯示在螢幕上的提示文字 (如 L"Name: ")。

Buffer: 接收輸入結果的緩衝區。

BufferSize: 緩衝區最大長度 (防止 Overflow)。

設計邏輯 (關鍵修復)：

使用獨立變數 EventIndex 傳入 WaitForEvent。

原因：若直接傳入計數器 Count，EDK2 會在事件觸發時將其歸零，導致輸入字元不斷被覆蓋。

支援 0x08 (Backspace) 處理，實現「游標倒退 -> 空白覆蓋 -> 游標倒退」的視覺刪除效果。

VOID GetGuidInput(OUT CHAR16 *Buffer)
功能：專門用於 GUID 的填空式輸入介面。

參數：

Buffer: 回傳格式化後的 GUID 字串 (36 chars)。

互動設計：

預先印出遮罩：________-____-____-____-____________。

自動跳號：當使用者輸入到第 8, 13, 18, 23 位時，游標自動跳過減號 (-)。

輸入限制：只接受 0-9, A-F (自動轉大寫)。

倒退修正：按下 Backspace 時，若遇到減號會自動多退一格。

2.2 資料轉換與解析 (Data Parsing)
UINTN MyStrToUintn(IN CHAR16 *Str)
功能：手寫的字串轉整數解析器 (取代 StrDecimalToUintn)。

解決痛點：

空白容錯：自動略過字串開頭的空白鍵 (Trim Leading Spaces)。

Hex/Dec 雙模：自動偵測 0x 前綴或內容是否包含 A-F，動態切換 10 進位或 16 進位模式。

邏輯：逐字元讀取，公式 Value = Value * Base + Digit。

EFI_STATUS StringToGuid(IN CHAR16 *Str, OUT EFI_GUID *Guid)
功能：將標準 36 字元字串轉換為 128-bit EFI_GUID 結構。

注意：Data1, Data2, Data3 為 Big Endian (直觀順序)，但 Data4 陣列在某些顯示實作上可能有 Endian 差異，此函式遵循 UEFI 標準字串格式。

2.3 顯示與業務邏輯 (Display & Logic)
VOID DumpHandleDetail(IN EFI_HANDLE Handle, IN UINTN Index)
功能：顯示單一 Handle 的所有詳細資訊。

顯示內容：

Handle Index / Address：區分列表序號與實體記憶體位址。

Driver Name：呼叫 GetDriverName 嘗試解析驅動名稱。

Device Path：將二進位路徑轉為可讀字串。

Protocols：列出所有安裝的 Protocol GUID 與名稱。

Interface Address：關鍵除錯資訊。即使 GUID 相同，透過 Interface 位址可證明這是不同的實體 (Instance)。

視覺設計：使用 ColorPrint 進行語法高亮 (Handle 為青色，Driver 為綠色，錯誤為紅色)。

CHAR16* GetDriverName(EFI_HANDLE Handle)
功能：嘗試透過 EFI_COMPONENT_NAME2_PROTOCOL 獲取驅動名稱。

用法：僅在 DXE Driver 有安裝此 Protocol 並支援 en-US 語言時有效。若失敗回傳 NULL。

2.4 系統底層 (System Infrastructure)
VOID OpenLogFile(IN EFI_HANDLE ImageHandle)
功能：初始化 Log 系統。

原理：

透過 LoadedImageProtocol 找到程式所在的 DeviceHandle。

開啟 SimpleFileSystemProtocol。

在根目錄建立 HandleDump_v10.log。

VOID Log(IN CONST CHAR16 *Format, ...)
功能：雙向輸出引擎 (Dual Output Engine)。

行為：

呼叫 gST->ConOut->OutputString 輸出到螢幕。

若 Log 檔案已開啟，將 Unicode 字串降轉為 ASCII 並寫入檔案 (方便 Windows 記事本查看)。

記憶體管理：內部使用 AllocatePool 暫存轉換後的 ASCII 字串，使用後立即 FreePool 防止洩漏。

3. 操作指令速查
上/下鍵：切換選單。

Enter：確認。

Backspace：刪除輸入字元。

Dump All：查看系統全貌。

Search GUID：填空介面，找特定功能的所有 Handle。

Search Index：直接輸入 Handle 編號 (如 A5) 查看詳細內容。
____________________________________________________________
cd /d D:\BIOS\MyWorkSpace\edk2

edksetup.bat Rebuild

chcp 65001

set PYTHONUTF8=1

set PYTHONIOENCODING=utf-8

rmdir /s /q Build\HandleProtocolgPkg

build -p HandleProtocolgPkg\HandleProtocolgPkg.dsc -a X64 -t VS2019 -b DEBUG
