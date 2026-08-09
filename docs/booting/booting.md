# Tài liệu Toàn tập về Quy trình Khởi động (Booting Process) trong Vi điều khiển ARM Cortex-M / STM32

## 1. Chuỗi Khởi động Hệ thống (Booting Sequence)

Khi vi điều khiển được cấp nguồn hoặc reset, phần cứng sẽ thực hiện chuỗi khởi động theo 3 bước cố định sau:

1. **Đọc trạng thái các chân BOOT0 và BOOT1**: Phần cứng kiểm tra mức điện áp trên các chân cấu hình này để xác định chế độ khởi động (Boot Mode) tương ứng.
2. **Nạp Con trỏ Ngăn xếp Chính (Fetch Main Stack Pointer - MSP)**: Hệ thống đọc giá trị từ địa chỉ gốc `0x00000000` để khởi tạo cho thanh ghi R13 (SP) (Stack Pointer). Giá trị này chính là đỉnh của vùng nhớ Stack (Top_of_Stack).
3. **Nạp Bộ đếm Chương trình (Fetch Program Counter - PC)**: Hệ thống đọc giá trị từ địa chỉ `0x00000004` để khởi tạo cho thanh ghi R15 (PC) (Program Counter). Giá trị này là địa chỉ của hàm xử lý Reset (Reset_Handler). Sau đó, vi điều khiển bắt đầu thực thi mã lệnh từ `Reset_Handler()`, nơi sẽ gọi đến hàm `main()`.

```text
Booting Sequence
[1] Determine Boot mode
[2] Fetch MSP from address 0x00000000
[3] Fetch PC from address 0x00000004
```

## 2. Bảng Vector Ngắt và các Ngoại lệ Hệ thống (Vector Table & System Exceptions)

Vùng nhớ từ địa chỉ `0x00000000` trở đi chứa bảng Vector biểu diễn địa chỉ của các hàm xử lý ngắt/ngoại lệ hệ thống (System Exceptions):

| Thứ tự | Địa chỉ Memory | Handler | Mô tả chức năng |
|---|---|---|---|
| - | 0x00000000 | Top_of_Stack | Giá trị khởi tạo cho thanh ghi Main Stack Pointer (MSP) |
| 1 | 0x00000004 | Reset_Handler | Giá trị khởi tạo cho Program Counter (PC). Trỏ đến hàm khởi động hệ thống và gọi `main()` |
| 2 | 0x00000008 | NMI_Handler | Ngắt không thể che chắn (Non-Maskable Interrupt) |
| 3 | 0x0000000C | HardFault_Handler | Lỗi phần cứng nghiêm trọng |
| 4 | 0x00000010 | MemManage_Handler | Lỗi quản lý bộ nhớ |
| 5 | 0x00000014 | BusFault_Handler | Lỗi Bus truy cập dữ liệu |
| 6 | 0x00000018 | UsageFault_Handler | Lỗi sử dụng tập lệnh / thực thi sai |
| 7-9 | 0x0000001C - 0x00000024 | Reserved | Vùng nhớ dự phòng |
| 10 | 0x00000028 | Reserved | Vùng nhớ dự phòng |
| 11 | 0x0000002C | SVC_Handler | Gọi dịch vụ hệ thống (Supervisor Call) |
| 12 | 0x00000030 | DebugMon_Handler | Trình giám sát gỡ lỗi (Debug Monitor) |
| 13 | 0x00000034 | Reserved | Vùng nhớ dự phòng |
| 14 | 0x00000038 | PendSV_Handler | Ngắt yêu cầu hệ thống treo (Pended Service) |
| 15 | 0x0000003C | SysTick_Handler | Ngắt định thời hệ thống (System Tick Timer) |
| 16 | 0x00000040 | WWDG_IRQHandler | Ngắt của Window Watchdog |

Minh họa nội dung `Reset_Handler`:

```c
// 0x00000004 ----> Value to initialize the Program Counter (PC)

void Reset_Handler() {
    ...
    main();
    ...
}
```

## 3. Trạng thái các Thanh ghi (Registers Status) lúc Khởi động

Ví dụ minh họa cụ thể từ bộ nhớ thực tế trong tài liệu:

- Tại địa chỉ `0x00000000`: Chứa giá trị `0x20001BB0`. Giá trị này sẽ được nạp trực tiếp vào thanh ghi R13 (SP) để định nghĩa đỉnh Stack (Initialize MSP).
- Tại địa chỉ `0x00000004`: Chứa giá trị địa chỉ của hàm Reset_Handler (ví dụ: `0x08000269`). Giá trị này được nạp vào thanh ghi R15 (PC).

> **Lưu ý kỹ thuật**: Bit 0 của địa chỉ nạp vào PC luôn luôn bằng 1 (ví dụ từ địa chỉ thực tế `0x08000268` đổi thành `0x08000269`) nhằm biểu thị vi điều khiển hoạt động ở trạng thái Thumb state (đây là quy định bắt buộc của lõi ARM Cortex-M).

## 4. Các Chế độ Khởi động (Boot Modes)

```text
[1] boot from main flash memory
[2] boot from system memory
[3] boot from embedded SRAM
```

Bản đồ bộ nhớ tổng quát (Memory Map):

```text
+-----------------------------------+ 0xFFFFFFFF
|              System               |
+-----------------------------------+ 0xE0000000
|          External Device          |
+-----------------------------------+ 0xA0000000
|           External RAM            |
+-----------------------------------+ 0x60000000
|            Peripheral             |
+-----------------------------------+ 0x40000000
|           Internal SRAM           |
+-----------------------------------+ 0x20000000
|               Code                |
+-----------------------------------+ 0x00000000
```

Chi tiết vùng Code Blocks:

```text
+-----------------------------------+ 0x1FFFFFFF
|           Option Bytes            |
+-----------------------------------+ 0x1FFF77FF
|      System Mem (Bootloader)      |
+-----------------------------------+ 0x1FFF0000
|             Reserved              |
+-----------------------------------+ 0x080X0000
|          Internal Flash           |
+-----------------------------------+ 0x08000000
|   Alias to selected Boot Memory   |
+-----------------------------------+ 0x00000000
```

Logic ánh xạ (remap) theo Boot Mode:

```text
**If bootmode == flash mem
---->   map 0x08000000 to 0x00000000
**If bootmode == System mem
---->   map 0x1FFF0000 to 0x00000000
**If bootmode == embedded SRAM
---->   map 0x20000000 to 0x00000000
```