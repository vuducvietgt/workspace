# SPI Kernel Module cho SSD1306 trên BeagleBone Black (BBB)

Tài liệu tổng hợp kiến thức nền tảng trước khi viết SPI device driver (kernel module) giao tiếp giữa BeagleBone Black và màn hình OLED SSD1306.

---

## 1. Giao thức SPI (Serial Peripheral Interface)

### 1.1 Tổng quan
SPI là giao thức truyền thông nối tiếp, đồng bộ (synchronous), song công toàn phần (full-duplex), theo mô hình **Master–Slave**. Không có địa chỉ như I2C — việc chọn thiết bị dựa vào đường **Chip Select (CS)** riêng cho từng slave.

### 1.2 4 đường tín hiệu chuẩn

| Tên chân | Ý nghĩa | Chiều (từ góc nhìn Master) |
|---|---|---|
| **SCLK** (SCK) | Serial Clock – xung clock do Master tạo ra | Output |
| **MOSI** | Master Out Slave In – dữ liệu Master gửi đi | Output |
| **MISO** | Master In Slave Out – dữ liệu Slave gửi về | Input |
| **CS/SS** | Chip Select (Slave Select) – chọn slave, active-low | Output |

> **Lưu ý với SSD1306 qua SPI**: SSD1306 là thiết bị **write-only** (chỉ nhận lệnh/dữ liệu từ MCU, không gửi gì về), nên **không dùng MISO**. Thay vào đó SSD1306 cần thêm 2 chân điều khiển riêng:
> - **D/C# (Data/Command)**: phân biệt byte đang gửi là lệnh hay dữ liệu hiển thị.
> - **RES# (Reset)**: reset cứng cho màn hình.
> Hai chân này **không thuộc chuẩn SPI**, phải điều khiển bằng GPIO thường (thường qua `gpiod`/`gpio_desc` trong kernel).

### 1.3 Đặc điểm chính
- **Đồng bộ**: dữ liệu dịch theo cạnh xung SCLK, không cần baud rate như UART.
- **Full-duplex**: MOSI và MISO hoạt động đồng thời (dù SSD1306 không dùng chiều về).
- **Không có ACK/NACK** như I2C — không có cơ chế báo lỗi ở tầng vật lý.
- **Tốc độ cao**: thường vài MHz đến vài chục MHz (SSD1306 chịu tối đa ~10 MHz tùy datasheet).
- **Thứ tự bit**: mặc định **MSB trước** (MSB-first) — SSD1306 yêu cầu MSB-first.
- **Kiến trúc bus**: một Master có thể nối nhiều Slave, mỗi Slave có 1 đường CS riêng (topology "star" cho CS, "daisy chain" cho SCLK/MOSI).

---

## 2. Thông tin chân pin trên BeagleBone Black

BBB (AM335x SoC) có 2 bus SPI khả dụng qua header mở rộng: **SPI0** và **SPI1** (SPI1 dùng chung một phần chân với eMMC nên cần cẩn thận nếu board dùng eMMC boot).

### 2.1 SPI0 (khuyến nghị dùng cho SSD1306, ít xung đột hơn)

| Tín hiệu | Pin header BBB | Tên mode trong Device Tree |
|---|---|---|
| SPI0_SCLK | **P9.22** | `spi0_sclk` |
| SPI0_D0 (MISO) | **P9.21** | `spi0_d0` |
| SPI0_D1 (MOSI) | **P9.18** | `spi0_d1` |
| SPI0_CS0 | **P9.17** | `spi0_cs0` |
| SPI0_CS1 | **P9.28** (tùy overlay) | `spi0_cs1` |

### 2.2 SPI1 (lưu ý: trùng chân với eMMC – nếu BBB boot từ eMMC, dùng SPI0 an toàn hơn)

| Tín hiệu | Pin header BBB |
|---|---|
| SPI1_SCLK | P9.31 |
| SPI1_D0 (MISO) | P9.29 |
| SPI1_D1 (MOSI) | P9.30 |
| SPI1_CS0 | P9.28 |

### 2.3 Chân GPIO bổ sung cho SSD1306

| Chức năng | Gợi ý pin (tùy chọn, cấu hình được) |
|---|---|
| D/C# (Data/Command) | ví dụ P9.15 (gpio1_16) |
| RES# (Reset) | ví dụ P9.23 (gpio1_17) |
| CS | dùng chung SPI0_CS0 (P9.17) hoặc GPIO rời nếu muốn điều khiển thủ công |
| VCC | 3.3V (P9.3 / P9.4) — **SSD1306 chuẩn là 3.3V logic**, kiểm tra module cụ thể |
| GND | P9.1 / P9.2 |

> Trước khi dùng các pin trên làm SPI/GPIO, phải cấu hình **pinmux** đúng mode (thường qua Device Tree Overlay), vì mặc định các pin này có thể đang ở chế độ GPIO hoặc chức năng khác.

---

## 3. Cấu hình (Device Tree + kernel driver)

### 3.1 Device Tree Overlay (`.dts`)

Kernel driver SPI trên Linux hoạt động dựa trên mô tả phần cứng trong **Device Tree**, không hard-code địa chỉ như bare-metal. Cần khai báo overlay để:

1. Cấu hình pinmux cho SPI0 + 2 GPIO (D/C, RES).
2. Kích hoạt controller `spi0` (`&spi0 { status = "okay"; }`).
3. Khai báo node con đại diện SSD1306 làm SPI slave.

Ví dụ khung overlay:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "ti,beaglebone-black";

    fragment@0 {
        target = <&am33xx_pinmux>;
        __overlay__ {
            spi0_pins: pinmux_spi0_pins {
                pinctrl-single,pins = <
                    0x150 0x30  /* spi0_sclk, INPUT_PULLUP | MODE0 */
                    0x154 0x30  /* spi0_d0 (miso) */
                    0x158 0x10  /* spi0_d1 (mosi), OUTPUT | MODE0 */
                    0x15c 0x10  /* spi0_cs0, OUTPUT | MODE0 */
                >;
            };
            ssd1306_pins: pinmux_ssd1306_pins {
                pinctrl-single,pins = <
                    0x048 0x0f  /* gpio D/C, OUTPUT */
                    0x04c 0x0f  /* gpio RESET, OUTPUT */
                >;
            };
        };
    };

    fragment@1 {
        target = <&spi0>;
        __overlay__ {
            status = "okay";
            #address-cells = <1>;
            #size-cells = <0>;
            pinctrl-names = "default";
            pinctrl-0 = <&spi0_pins>;

            ssd1306@0 {
                compatible = "mycomp,ssd1306-spi";
                reg = <0>;              /* CS0 */
                spi-max-frequency = <8000000>;
                spi-cpol;               /* nếu dùng SPI Mode 3 */
                spi-cpha;
                dc-gpios  = <&gpio1 16 0>;
                reset-gpios = <&gpio1 17 0>;
            };
        };
    };
};
```

Biên dịch: `dtc -O dtb -o ssd1306.dtbo -b 0 -@ ssd1306-overlay.dts`, sau đó nạp qua `u-boot overlays` hoặc `config-pin`.

### 3.2 Các thông số SPI cần cấu hình trong driver (`struct spi_device`)

| Tham số | Ý nghĩa | Giá trị điển hình cho SSD1306 |
|---|---|---|
| `mode` | Chọn CPOL/CPHA (SPI Mode 0–3) | `SPI_MODE_0` hoặc `SPI_MODE_3` (SSD1306 hỗ trợ cả 2) |
| `bits_per_word` | Số bit mỗi word truyền | 8 |
| `max_speed_hz` | Tốc độ clock tối đa | tùy module, thường 1–10 MHz |
| `chip_select` | Số CS sử dụng | 0 (CS0) |
| `cs_gpio` / `cs-gpios` | Nếu CS điều khiển bằng GPIO rời | tùy board |

### 3.3 Khung kernel module (SPI driver)

```c
static const struct of_device_id ssd1306_of_match[] = {
    { .compatible = "mycomp,ssd1306-spi" },
    { }
};
MODULE_DEVICE_TABLE(of, ssd1306_of_match);

static int ssd1306_probe(struct spi_device *spi)
{
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 8000000;
    spi_setup(spi);

    /* lấy gpio D/C, RESET qua devm_gpiod_get(), init phần cứng SSD1306 ... */
    return 0;
}

static void ssd1306_remove(struct spi_device *spi)
{
    /* cleanup */
}

static struct spi_driver ssd1306_driver = {
    .driver = {
        .name = "ssd1306",
        .of_match_table = ssd1306_of_match,
    },
    .probe = ssd1306_probe,
    .remove = ssd1306_remove,
};
module_spi_driver(ssd1306_driver);
```

Truyền dữ liệu dùng `spi_write()` / `spi_sync()` với `struct spi_transfer`, kèm điều khiển chân D/C bằng `gpiod_set_value()` trước mỗi lần gửi lệnh hay dữ liệu.

---

## 4. Dạng Waveform (giản đồ tín hiệu)

### 4.1 4 chế độ SPI (SPI Mode) — xác định bởi CPOL và CPHA

| Mode | CPOL | CPHA | Trạng thái nghỉ SCLK | Cạnh lấy mẫu dữ liệu |
|---|---|---|---|---|
| 0 | 0 | 0 | Thấp (Low) | Cạnh lên đầu tiên (rising edge) |
| 1 | 0 | 1 | Thấp (Low) | Cạnh xuống thứ hai |
| 2 | 1 | 0 | Cao (High) | Cạnh xuống đầu tiên |
| 3 | 1 | 1 | Cao (High) | Cạnh lên thứ hai |

- **CPOL** (Clock Polarity): xác định mức nghỉ (idle) của SCLK khi không truyền dữ liệu.
- **CPHA** (Clock Phase): xác định dữ liệu được **lấy mẫu (sample)** ở cạnh đầu tiên hay cạnh thứ hai của mỗi chu kỳ clock.
- **SSD1306** theo datasheet hỗ trợ **Mode 0** và **Mode 3** — hai mode này có đặc tính lấy mẫu giống nhau về mặt "sample trên cạnh ổn định", chỉ khác mức nghỉ của clock.

### 4.2 Waveform minh họa (Mode 0 — CPOL=0, CPHA=0)

```
CS   ‾‾\_________________________________________/‾‾
                                                  
SCLK ____/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\__________
             1   2   3   4   5   6   7   8
MOSI  ---<D7><D6><D5><D4><D3><D2><D1><D0>---------
         (dữ liệu ổn định TRƯỚC cạnh lên,
          Master/Slave LẤY MẪU tại cạnh lên)
```

- **CS** kéo xuống Low để bắt đầu 1 phiên truyền (frame), giữ Low suốt quá trình gửi byte, kéo lên High khi kết thúc.
- **MOSI** phải **ổn định (setup)** trước cạnh lấy mẫu và giữ nguyên qua cạnh đó (hold time).
- Dữ liệu truyền theo thứ tự **MSB trước (D7 → D0)**.
- Với SSD1306: mỗi byte gửi đi được hiểu là **Command** hay **Data** tùy trạng thái chân **D/C#** tại thời điểm đó (D/C# = 0: command, D/C# = 1: data) — chân này phải được set **trước khi** CS xuống Low và giữ ổn định suốt byte đó.

### 4.3 Timing tổng thể một giao dịch (transaction)

```
D/C# ----X=================X-----------------
CS   ‾‾\_______(8 xung clock)_______/‾‾\______
SCLK  ________/‾\/‾\/‾\/‾\/‾\/‾\/‾\/‾\_________
MOSI  ________[........8 bit data........]____
```

---

## 5. Xung Clock và Start/Stop Trigger

### 5.1 Vai trò xung Clock (SCLK)
- SCLK **luôn do Master phát ra** — SSD1306 (slave) không tự tạo clock.
- Mỗi xung clock dịch (shift) đúng **1 bit** dữ liệu ra/vào.
- Tần số SCLK xác định tốc độ truyền: `throughput ≈ f_SCLK / 8` byte/giây (đơn giản hóa, chưa tính overhead).
- Với SSD1306, cần tra datasheet của **module cụ thể** đang dùng (không phải chip trần) vì mạch driver phụ trợ trên module có thể giới hạn tốc độ thấp hơn giá trị lý thuyết của IC gốc (thường an toàn ở mức 1–4 MHz nếu không rõ, có thể thử tăng dần tới 8-10MHz).

### 5.2 Start / Stop trigger (bắt đầu/kết thúc 1 giao dịch)

Không giống I2C có START/STOP condition đặc trưng bằng cách đổi mức SDA khi SCL đang High, SPI dùng **CS (Chip Select)** làm tín hiệu "khung" (framing):

| Sự kiện | Hành động |
|---|---|
| **Start (bắt đầu)** | CS chuyển từ High → Low (active-low). Đây là "trigger" báo cho Slave biết Master chuẩn bị gửi/nhận dữ liệu. |
| **Trong khi truyền** | CS giữ nguyên mức Low suốt toàn bộ frame (có thể nhiều byte liên tiếp nếu Slave hỗ trợ). |
| **Stop (kết thúc)** | CS chuyển từ Low → High. Slave hiểu đây là kết thúc giao dịch, "chốt" (latch) dữ liệu cuối cùng nếu cần. |

**Yêu cầu timing quan trọng:**
- **CS setup time**: thời gian tối thiểu giữa lúc CS xuống Low và xung SCLK đầu tiên (đảm bảo Slave kịp "tỉnh dậy" và giải mã địa chỉ/chọn kênh nội bộ).
- **CS hold time**: thời gian tối thiểu giữ CS Low sau xung SCLK cuối cùng trước khi kéo lên lại.
- **Idle time giữa 2 CS pulse** (nếu gửi nhiều lệnh liên tiếp): SSD1306 cần một khoảng nghỉ tối thiểu giữa các giao dịch (tham khảo thông số `t_CSS`, `t_CSH`, `t_CSW` trong datasheet SSD1306, thường ở mức vài chục ns đến 100ns).

### 5.3 Trong kernel — ai điều khiển CS?

- Với controller SPI của AM335x, **CS thường được điều khiển tự động bởi phần cứng SPI (McSPI)** theo cấu hình `spi_transfer`/`spi_message` — không cần toggling tay trong hầu hết trường hợp.
- Nếu cần kiểm soát chi tiết hơn (ví dụ giữ CS Low qua nhiều `spi_transfer` để gộp lệnh), dùng cờ `cs_change` trong `struct spi_transfer`:
  - `cs_change = 0`: giữ nguyên CS sau transfer này (không toggle) nếu còn transfer tiếp theo trong cùng message.
  - `cs_change = 1`: toggle CS sau transfer đó.

---

## Tóm tắt luồng thiết kế driver

1. Viết Device Tree Overlay khai báo pinmux cho SPI0 + 2 GPIO (D/C, RESET).
2. Viết `spi_driver` với `probe()` cấu hình `mode`, `bits_per_word`, `max_speed_hz`.
3. Trong `probe()`: reset màn hình qua GPIO RESET, gửi chuỗi lệnh khởi tạo SSD1306 (qua `spi_write` với D/C#=0).
4. Cung cấp giao diện ghi dữ liệu hiển thị (ví dụ qua `/dev/ssd1306` hoặc tích hợp framebuffer) — gửi qua `spi_write` với D/C#=1.
5. Đảm bảo timing CS/D-C tuân theo datasheet SSD1306 để tránh lỗi hiển thị.

---

### Tài liệu tham khảo nên đọc thêm
- Datasheet **SSD1306** (Solomon Systech) — mục "SPI Interface" (mode 0/3, timing `t_CSS/t_CSH/t_CSW`, tần số tối đa).
- **AM335x Technical Reference Manual** — chương McSPI (Multichannel SPI controller).
- Kernel doc: `Documentation/spi/spi-summary.rst`, `include/linux/spi/spi.h`.
- BeagleBone Black **System Reference Manual** — bảng pinout P8/P9 header.