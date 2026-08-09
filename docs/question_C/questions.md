# Nhật ký câu hỏi C

## Mục lục theo chủ đề

- [1. Sizeof struct với union và bitfield](#1-sizeof-struct-với-union-và-bitfield-packed) — *Union, Bitfield, Struct*
- [2. Ưu điểm và ứng dụng của union](#2-ưu-điểm-và-ứng-dụng-của-union) — *Union*
- [3. Ý nghĩa bitfield uint8_t dry 4](#3-ý-nghĩa-bitfield-uint8_t-dry-4) — *Bitfield*
- [4. MSB, LSB là gì, áp dụng ở đâu](#4-msb-lsb-là-gì-áp-dụng-ở-đâu-trong-embedded) — *Endianness, Embedded*
- [5. Convert char sang int không dùng API](#5-convert-char-sang-int-không-dùng-api-atoi-strtol) — *String, Parsing*
- [6. Xử lý overflow khi result vượt giới hạn int](#6-xử-lý-overflow-khi-result-vượt-giới-hạn-int) — *Overflow, Undefined Behavior*
- [7. Giá trị INT_MAX và INT_MIN](#7-giá-trị-int_max-và-int_min) — *Limits, Data Types*

---

## 1. sizeof struct với union và bitfield packed

**Câu hỏi:** Tại sao union chỉ chiếm 1 byte, có phải các thành viên union đều dùng chung 1 vùng nhớ?

**Trả lời ngắn:** Đúng. Các thành viên union **overlap** tại cùng địa chỉ (offset 0), `sizeof(union)` = kích thước thành viên lớn nhất.

```c
union {
    uint8_t used_count;
    uint8_t is_saved;
} u;
u.used_count = 5;
printf("%d", u.is_saved); // 5 - cùng 1 byte
```

[⬆ Về mục lục](#mục-lục-theo-chủ-đề)

---

## 2. Ưu điểm và ứng dụng của union

**Câu hỏi:** Ưu điểm của union là gì, áp dụng ở đâu? Có phải tiết kiệm bộ nhớ?

**Trả lời ngắn:**
- **Tiết kiệm bộ nhớ**: chỉ cấp phát = kích thước thành viên lớn nhất, dùng khi các field loại trừ lẫn nhau.
- **Type punning**: diễn giải lại cùng vùng nhớ theo nhiều cách (ví dụ tách byte của số nguyên).
- **Tagged union**: mô phỏng kiểu dữ liệu "một trong nhiều dạng", thường đi kèm biến `type` để biết field nào đang dùng.
- Ứng dụng: lập trình nhúng (thanh ghi phần cứng), parse gói tin mạng, interpreter/VM, game engine (Vector/Color).
- Nhược điểm: dữ liệu overlap, dễ bug nếu ghi nhầm field; type punning qua union là UB trong C++ (C thì được phép nhưng phụ thuộc compiler/endianness).

```c
union {
    uint32_t raw;
    uint8_t bytes[4];
} packet;
packet.raw = 0x12345678; // truy cập từng byte qua packet.bytes[i]
```

[⬆ Về mục lục](#mục-lục-theo-chủ-đề)

---

## 3. Ý nghĩa bitfield uint8_t dry 4

**Câu hỏi:** Khai báo `uint8_t dry:4;` có ý nghĩa gì?

**Trả lời ngắn:**
- Đây là **bitfield** — biến chỉ chiếm 4 bit (giá trị 0-15), thay vì nguyên 8 bit.
- Dùng để **tiết kiệm bộ nhớ** khi giá trị nhỏ, và **ghép nhiều field** vào chung 1 byte.
- Không lấy được địa chỉ (`&dry` lỗi), thứ tự bit (MSB/LSB trước) phụ thuộc compiler → không portable 100%.
- Ứng dụng: thanh ghi phần cứng, cờ trạng thái, giao thức nhị phân.

```c
struct {
    uint8_t dry:4;   // 4 bit
    uint8_t wash:4;  // 4 bit
} mode; // gộp lại = 1 byte thay vì 2 byte

mode.dry = 5;   // OK (0-15)
mode.dry = 20;  // bị truncate còn 4 (20 & 0xF)
```

[⬆ Về mục lục](#mục-lục-theo-chủ-đề)

---

## 4. MSB, LSB là gì, áp dụng ở đâu trong embedded

**Câu hỏi:** MSB, LSB là gì? Áp dụng ở đâu, đặc biệt trong embedded?

**Trả lời ngắn:**
- **MSB** (Most Significant Bit): bit có trọng số lớn nhất (vị trí cao nhất).
- **LSB** (Least Significant Bit): bit có trọng số nhỏ nhất (vị trí thấp nhất).
- Big-endian = MSB-first (gửi/lưu bit/byte lớn trước), Little-endian = LSB-first.
- Ứng dụng embedded: giao tiếp UART/SPI/I2C (bit order), network byte order (`htons`, `htonl`), thanh ghi phần cứng, bitfield trong struct, CRC/checksum, đọc giá trị ADC nhiều byte.
- Sai thứ tự MSB/LSB giữa 2 thiết bị → dữ liệu nhận sai hoàn toàn dù truyền không lỗi, phải đọc kỹ datasheet.

```c
uint8_t msb = 0x0A, lsb = 0xF0;
uint16_t adc_value = ((uint16_t)msb << 8) | lsb; // ghép 2 byte thành giá trị 16-bit
```

[⬆ Về mục lục](#mục-lục-theo-chủ-đề)

---

## 5. Convert char sang int không dùng API (atoi, strtol)

**Câu hỏi:** Chuyển char sang integer trong C, không dùng API có sẵn?

**Trả lời ngắn:**
- 1 ký tự số: `int value = c - '0';` (vì `'0'`-`'9'` liên tiếp trong bảng mã).
- Cả chuỗi số: tự viết vòng lặp, dồn kết quả bằng `result = result * 10 + (str[i] - '0')`.

```c
int my_atoi(const char *str) {
    int result = 0, sign = 1, i = 0;
    if (str[i] == '-') { sign = -1; i++; }
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    return result * sign;
}
// my_atoi("-1234") -> -1234
```

[⬆ Về mục lục](#mục-lục-theo-chủ-đề)

---

## 6. Xử lý overflow khi result vượt giới hạn int

**Câu hỏi:** Trong hàm atoi tự viết, nếu result overflow kiểu int thì handle thế nào?

**Trả lời ngắn:**
- Overflow của `int` có dấu là **undefined behavior** trong C → không thể để tràn rồi mới check.
- Phải kiểm tra **trước** khi thực hiện phép nhân/cộng, bằng cách biến đổi bất đẳng thức: `result > (INT_MAX - digit) / 10` thay vì `result * 10 + digit > INT_MAX`.
- Cách khác: dùng kiểu rộng hơn (`int64_t`) để tính tạm rồi clamp — dễ đọc hơn nhưng tốn tài nguyên trên MCU nhỏ.

```c
#include <limits.h>
if (result > (INT_MAX - digit) / 10) {
    return (sign == 1) ? INT_MAX : INT_MIN; // clamp
}
result = result * 10 + digit;
```

[⬆ Về mục lục](#mục-lục-theo-chủ-đề)

---

## 7. Giá trị INT_MAX và INT_MIN

**Câu hỏi:** INT_MAX và INT_MIN có giá trị bao nhiêu?

**Trả lời ngắn:**
- Với `int` 32-bit (bù 2): `INT_MAX = 2147483647` (`0x7FFFFFFF`), `INT_MIN = -2147483648` (`0x80000000`).
- Số âm nhiều hơn số dương 1 đơn vị vì bù 2 có 1 pattern bit (`1000...0`) không có số dương đối xứng.
- Trên MCU 16-bit: `INT_MAX = 32767`, `INT_MIN = -32768`.
- Luôn dùng macro trong `<limits.h>`, không hard-code số, vì `int` không đảm bảo luôn 32-bit.

```c
#include <limits.h>
printf("%d %d\n", INT_MAX, INT_MIN); // 2147483647 -2147483648
```

[⬆ Về mục lục](#mục-lục-theo-chủ-đề)

---