# HandleProtocolgPkg
---

# UEFI Handle Inspector (HandleProtocolg) 技術文件

## 1. 專案概述

本工具是一個在 UEFI Shell 下執行的診斷程式，用於遍歷、搜尋與分析系統中的 Handle 資料庫。
主要解決了 EDK2 標準庫在輸入解析上的局限性，並提供了圖形化選單 (TUI) 與自動 Log 功能。

---

## 2. 核心函數庫 (API Reference)

### 2.1 輸入處理系統 (Input Handling)

#### `VOID GetInput(IN CHAR16 *Prompt, OUT CHAR16 *Buffer, IN UINTN BufferSize)`

**功能**：通用的文字輸入介面，支援 Backspace 修改。

* **參數**：
* `Prompt`: 顯示在螢幕上的提示文字 (如 `L"Name: "`)。
* `Buffer`: 接收輸入結果的緩衝區。
* `BufferSize`: 緩衝區最大長度 (防止 Overflow)。


* **設計邏輯 (關鍵修復)**：
* 使用獨立變數 `EventIndex` 傳入 `WaitForEvent`。
* **原因**：若直接傳入計數器 `Count`，EDK2 會在事件觸發時將其歸零，導致輸入字元不斷被覆蓋。
* 支援 `0x08` (Backspace) 處理，實現「游標倒退 -> 空白覆蓋 -> 游標倒退」的視覺刪除效果。



#### `VOID GetGuidInput(OUT CHAR16 *Buffer)`

**功能**：專門用於 GUID 的填空式輸入介面。

* **參數**：
* `Buffer`: 回傳格式化後的 GUID 字串 (36 chars)。


* **互動設計**：
* 預先印出遮罩：`________-____-____-____-____________`。
* **自動跳號**：當使用者輸入到第 8, 13, 18, 23 位時，游標自動跳過減號 (`-`)。
* **輸入限制**：只接受 `0-9`, `A-F` (自動轉大寫)。
* **倒退修正**：按下 Backspace 時，若遇到減號會自動多退一格。



---

### 2.2 資料轉換與解析 (Data Parsing)

#### `UINTN MyStrToUintn(IN CHAR16 *Str)`

**功能**：手寫的字串轉整數解析器 (取代 `StrDecimalToUintn`)。

* **解決痛點**：
1. **空白容錯**：自動略過字串開頭的空白鍵 (Trim Leading Spaces)。
2. **Hex/Dec 雙模**：自動偵測 `0x` 前綴或內容是否包含 `A-F`，動態切換 10 進位或 16 進位模式。


* **邏輯**：逐字元讀取，公式 `Value = Value * Base + Digit`。

#### `EFI_STATUS StringToGuid(IN CHAR16 *Str, OUT EFI_GUID *Guid)`

**功能**：將標準 36 字元字串轉換為 128-bit `EFI_GUID` 結構。

* **注意**：`Data1`, `Data2`, `Data3` 為 Big Endian (直觀順序)，但 `Data4` 陣列在某些顯示實作上可能有 Endian 差異，此函式遵循 UEFI 標準字串格式。

---

### 2.3 顯示與業務邏輯 (Display & Logic)

#### `VOID DumpHandleDetail(IN EFI_HANDLE Handle, IN UINTN Index)`

**功能**：顯示單一 Handle 的所有詳細資訊。

* **顯示內容**：
1. **Handle Index / Address**：區分列表序號與實體記憶體位址。
2. **Driver Name**：呼叫 `GetDriverName` 嘗試解析驅動名稱。
3. **Device Path**：將二進位路徑轉為可讀字串。
4. **Protocols**：列出所有安裝的 Protocol GUID 與名稱。
5. **Interface Address**：**關鍵除錯資訊**。即使 GUID 相同，透過 Interface 位址可證明這是不同的實體 (Instance)。


* **視覺設計**：使用 `ColorPrint` 進行語法高亮 (Handle 為青色，Driver 為綠色，錯誤為紅色)。

#### `CHAR16* GetDriverName(EFI_HANDLE Handle)`

**功能**：嘗試透過 `EFI_COMPONENT_NAME2_PROTOCOL` 獲取驅動名稱。

* **用法**：僅在 DXE Driver 有安裝此 Protocol 並支援 `en-US` 語言時有效。若失敗回傳 `NULL`。

---

### 2.4 系統底層 (System Infrastructure)

#### `VOID OpenLogFile(IN EFI_HANDLE ImageHandle)`

**功能**：初始化 Log 系統。

* **原理**：
1. 透過 `LoadedImageProtocol` 找到程式所在的 `DeviceHandle`。
2. 開啟 `SimpleFileSystemProtocol`。
3. 在根目錄建立 `HandleDump_v10.log`。



#### `VOID Log(IN CONST CHAR16 *Format, ...)`

**功能**：雙向輸出引擎 (Dual Output Engine)。

* **行為**：
1. 呼叫 `gST->ConOut->OutputString` 輸出到螢幕。
2. 若 Log 檔案已開啟，將 Unicode 字串降轉為 ASCII 並寫入檔案 (方便 Windows 記事本查看)。


* **記憶體管理**：內部使用 `AllocatePool` 暫存轉換後的 ASCII 字串，使用後立即 `FreePool` 防止洩漏。

---

## 3. 編譯與依賴 (Build Dependencies)

若要編譯此專案，請確保 `.inf` 檔案包含以下依賴，否則會出現 `LNK2001` 錯誤。

**必要的 Libraries:**

```ini
[LibraryClasses]
  UefiApplicationEntryPoint
  UefiLib
  UefiBootServicesTableLib
  MemoryAllocationLib
  BaseMemoryLib
  DevicePathLib  # 用於 Device Path 轉字串
  PrintLib       # 用於 UnicodeSPrint

```

**必要的 Protocols:**

```ini
[Protocols]
  gEfiComponentName2ProtocolGuid  # 用於 GetDriverName
  gEfiLoadedImageProtocolGuid     # 用於 Log 系統定位
  gEfiSimpleFileSystemProtocolGuid
  # ... (其他欲查詢的 Protocol GUID)

```

---

## 4. 常見問題排除 (Troubleshooting)

| 問題現象 | 原因 | 解決方案 |
| --- | --- | --- |
| **LNK1104: cannot open file** | 上次執行的 `.efi` 或 `.dll` 仍被模擬器或 OS 鎖定。 | 關閉 QEMU/VMware，或執行 `build clean`。 |
| **LNK2005: CharToUpper defined** | 自定義函數與 `BaseLib` 名稱衝突。 | 將函數改名為 `MyCharToUpper`。 |
| **輸入數字解析為 0** | 舊版 `GetInput` 變數衝突導致字串為空，或開頭有空白鍵。 | 使用 v10 版 `MyStrToUintn` (自動 Trim 空白) 與修復版 `GetInput`。 |
| **找不到 Protocol Name** | 大小寫不匹配或字串比對邏輯錯誤。 | 使用 `MyStrCmpi` 進行 Case-Insensitive 比對。 |

---

## 5. 操作指令速查

* **上/下鍵**：切換選單。
* **Enter**：確認。
* **Backspace**：刪除輸入字元。
* **Dump All**：查看系統全貌。
* **Search GUID**：填空介面，找特定功能的所有 Handle。
* **Search Index**：直接輸入 Handle 編號 (如 `A5`) 查看詳細內容。

這是一份為 `HandleProtocolg` (v10 旗艦版) 撰寫的**「系統架構與主選單流程 (System Architecture & Menu Flow)」**教科書級說明章節。您可以將這段內容直接無縫接軌到您的 README 技術筆記中。

這章節將剖析本工具是如何與 UEFI 底層的 Handle Database (控制代碼資料庫) 互動，以及主程式的生命週期 (Lifecycle) 與狀態機 (State Machine) 流程。

---

# 系統架構與主選單流程 (System Architecture & Menu Flow)

## 1. UEFI 系統架構概念 (UEFI System Concepts)

要理解本工具的運作原理，首先必須釐清 UEFI Driver Model 的「三本柱」：**Handle、Protocol 與 Interface**。本工具的核心任務，就是將這三者的抽象關係具象化並印在螢幕上。

* **Handle (控制代碼)**：
可以把它想像成一個「容器」或「身分證字號」。在 UEFI 系統中，每一個硬體裝置 (如 USB 控制器、硬碟)、每一個虛擬裝置或每一個載入的執行檔 (.efi)，都會被系統分配一個獨一無二的 Handle。
* *本工具實作*：透過 `gBS->LocateHandleBuffer(AllHandles, ...)` 一次性將系統中所有的 Handle 抓取到記憶體陣列中。


* **Protocol (協定)**：
附著在 Handle 上的「能力標籤」。Protocol 是一組由 C 語言 `struct` 組成的函式指標與資料變數。每一個 Protocol 都有一個 128-bit 的 GUID 作為全球唯一識別碼。
* *本工具實作*：透過 `gBS->ProtocolsPerHandle` 查詢某個 Handle 上掛載了哪些 GUID，並透過內建的 `mProtocolDatabase` 將生硬的 GUID 翻譯成人類可讀的名稱 (如 `BlockIO`)。


* **Interface (介面實體位址)**：
這是 Protocol 在記憶體中真正的存放位址。**不同的 Handle 可能擁有相同 GUID 的 Protocol，但它們的 Interface 位址絕對不同。**
* *本工具實作*：透過 `gBS->HandleProtocol` 取得該 Protocol 的 Interface 指標，這對於開發者除錯「哪一個驅動程式實作了這個介面」至關重要。



---

## 2. 工具軟體架構層 (Software Architecture Layers)

`HandleProtocolg` 的程式碼設計採用了清晰的分層架構 (Layered Architecture)：

1. **輸入層 (Input / Event Layer)**：
* **組件**：`GetInput()`, `GetGuidInput()`, `RunMenu()`
* **職責**：直接與 `gST->ConIn` 互動。攔截鍵盤的 `ScanCode` (方向鍵) 與 `UnicodeChar` (字元與 Backspace)。使用 `WaitForEvent` 實作非阻塞 (Non-blocking) 式的等待，確保系統資源不被霸佔。


2. **處理層 (Processing / Logic Layer)**：
* **組件**：`MyStrToUintn()`, `StringToGuid()`, `MyStrCmpi()`
* **職責**：將使用者的「字串輸入」安全地轉換為「系統資料結構」(整數或 GUID)。提供高度容錯能力 (如忽略空白、大小寫轉換、自動跳過減號)。


3. **核心查詢層 (Core Query Layer)**：
* **組件**：`SearchByProtocol()`, `GetDriverName()`
* **職責**：與 UEFI Boot Services (`gBS`) 直接交火。負責遍歷 Handle Database，並使用 `EFI_COMPONENT_NAME2_PROTOCOL` 嘗試解析底層驅動程式的名稱。


4. **輸出層 (Output / Logging Layer)**：
* **組件**：`DumpHandleDetail()`, `ColorPrint()`, `Log()`
* **職責**：將查詢結果格式化。使用 `gST->ConOut->SetAttribute` 進行語法高亮 (Syntax Highlighting)，並透過 `EFI_FILE_PROTOCOL` 將結果同步持久化 (Persist) 到磁碟中的 `HandleDump_v10.log`。



---

## 3. 主選單執行流程 (Main Menu Flow)

程式的生命週期由 `UefiMain` 控制，整體流程為一個無限迴圈 (Infinite Loop) 所構成的狀態機，直到使用者選擇 `Exit` 才會觸發清理並退出。

```mermaid
graph TD
    %% 節點定義
    Start([啟動: UefiMain])
    InitLog[初始化 Log 系統<br>OpenLogFile]
    MenuLoop{進入主選單<br>RunMenu}
    
    Case0[Case 0: Dump All<br>列舉所有 Handle]
    Case1[Case 1: Search GUID<br>填空模式輸入 GUID]
    Case2[Case 2: Search Name<br>模糊/精準名稱查表]
    Case3[Case 3: Search Index<br>智慧數字解析 (Hex/Dec)]
    
    PrintResult[核心顯示邏輯<br>DumpHandleDetail]
    WaitKey[等待任意鍵<br>WaitForKey]
    Cleanup[關閉 Log 檔案<br>CloseLogFile]
    End([結束程式: EFI_SUCCESS])

    %% 流程線
    Start --> InitLog
    InitLog --> MenuLoop
    
    %% 選單分支
    MenuLoop -->|選擇 1| Case0
    MenuLoop -->|選擇 2| Case1
    MenuLoop -->|選擇 3| Case2
    MenuLoop -->|選擇 4| Case3
    MenuLoop -->|選擇 Exit| Cleanup
    
    %% 執行邏輯
    Case0 --> PrintResult
    Case1 --> |StringToGuid| PrintResult
    Case2 --> |GetGuidByName| PrintResult
    Case3 --> |MyStrToUintn| PrintResult
    
    %% 迴圈返回
    PrintResult --> WaitKey
    WaitKey -.-> |清除緩衝區並返回| MenuLoop
    
    %% 結束
    Cleanup --> End

```

### 流程拆解說明 (Step-by-Step Breakdown)：

1. **啟動與環境準備 (Initialization)**：
進入 `UefiMain` 後，第一步是呼叫 `OpenLogFile(ImageHandle)`。系統會追溯此 `.efi` 檔案所在的磁碟位置，並在該磁碟根目錄建立 Log 檔。這確保了即使電腦重開機，剛才的檢測記錄依然保留。
2. **UI 渲染與輪詢 (UI Render & Polling)**：
呼叫 `RunMenu()`。此時關閉游標顯示 (`EnableCursor(FALSE)`)，進入鍵盤事件輪詢 (`WaitForEvent`)。根據使用者的上下鍵改變 `Selection` 變數，並透過 `SetAttribute` 動態重繪反白效果，直到使用者按下 Enter (`\r`) 回傳選擇的 Index。
3. **分支執行 (Branch Execution)**：
* **Dump All**: 直接呼叫 `LocateHandleBuffer`，並用 `for` 迴圈將回傳的陣列從 0 印到最大值。
* **Search GUID**: 呼叫客製化的 `GetGuidInput` 顯示遮罩。輸入完成後，將字串轉為 128-bit GUID，傳遞給 `SearchByProtocol` 進行全系統比對。
* **Search Name**: 讓使用者輸入字串 (如 `BlockIO`)，先到 `mProtocolDatabase` 進行不分大小寫比對 (`MyStrCmpi`)，若查無此名則直接報錯，若有則取得對應 GUID 交由 `SearchByProtocol` 處理。
* **Search Index**: 讓使用者輸入數字，交由 `MyStrToUintn` 自動判斷進位制並解析。確認數字未超出最大 Handle 數量後，直接針對該陣列索引呼叫 `DumpHandleDetail`。


4. **結果展示與暫停 (Result & Pause)**：
結果輸出完畢後，印出 `Press any key to return...`。為了避免使用者剛才按下的 Enter 鍵殘留在緩衝區導致直接跳過暫停，這裡會先用 `gST->ConIn->ReadKeyStroke` 清空舊按鍵，再呼叫一次 `WaitForEvent` 等待新的按鍵行為。
5. **安全退出 (Graceful Exit)**：
當使用者在選單選擇 Exit (選項 4)，跳出 `while(TRUE)` 迴圈，呼叫 `CloseLogFile()` 強制將記憶體中的資料寫入磁碟 (Flush) 並關閉指標，最後回傳 `EFI_SUCCESS` 將控制權交還給 UEFI Shell。
____________________________________________________________
cd /d D:\BIOS\MyWorkSpace\edk2

edksetup.bat Rebuild

chcp 65001

set PYTHONUTF8=1

set PYTHONIOENCODING=utf-8

rmdir /s /q Build\HandleProtocolgPkg

build -p HandleProtocolgPkg\HandleProtocolgPkg.dsc -a X64 -t VS2019 -b DEBUG
