from html import escape
from pathlib import Path

import cairosvg
from PIL import Image


ROOT = Path(__file__).resolve().parent
IMAGE_DIR = ROOT / "images"
SVG_DIR = ROOT / ".render"

SANS = "Microsoft YaHei, Noto Sans SC, sans-serif"
MONO = "Cascadia Mono, Consolas, monospace"

INK = "#17212B"
MUTED = "#596673"
LINE = "#263746"
GRID = "#D4DBE1"
BLUE = "#1F5D8C"
RED = "#9B3A36"
GREEN = "#326B55"
AMBER = "#8A6421"
PAPER = "#FFFFFF"
SOFT = "#F4F6F8"


def add_text(
    parts,
    x,
    y,
    value,
    size=20,
    fill=INK,
    weight=400,
    anchor="start",
    font=SANS,
    opacity=1,
):
    parts.append(
        f'<text x="{x}" y="{y}" font-family="{font}" font-size="{size}" '
        f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}" '
        f'opacity="{opacity}">{escape(str(value))}</text>'
    )


def add_line(
    parts,
    x1,
    y1,
    x2,
    y2,
    color=LINE,
    width=1.8,
    dash=None,
    arrow=None,
):
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    marker_attr = f' marker-end="url(#{arrow})"' if arrow else ""
    parts.append(
        f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        f'stroke="{color}" stroke-width="{width}"{dash_attr}{marker_attr}/>'
    )


def add_path(
    parts,
    points,
    color=LINE,
    width=1.8,
    dash=None,
    arrow=None,
    fill="none",
):
    point_text = " ".join(
        f"{'M' if index == 0 else 'L'} {x} {y}"
        for index, (x, y) in enumerate(points)
    )
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    marker_attr = f' marker-end="url(#{arrow})"' if arrow else ""
    parts.append(
        f'<path d="{point_text}" fill="{fill}" stroke="{color}" '
        f'stroke-width="{width}" stroke-linecap="square" '
        f'stroke-linejoin="miter"{dash_attr}{marker_attr}/>'
    )


def add_rect(
    parts,
    x,
    y,
    w,
    h,
    fill="none",
    stroke=LINE,
    width=1.8,
    dash=None,
):
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    parts.append(
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" '
        f'stroke="{stroke}" stroke-width="{width}"{dash_attr}/>'
    )


def add_title(parts, width, project, subtitle):
    add_text(parts, 64, 55, project, 31, INK, weight=600)
    add_text(parts, 64, 86, subtitle, 17, MUTED)
    add_text(parts, width - 64, 55, "TECHNICAL BLOCK DIAGRAM", 14, MUTED, font=MONO, anchor="end")
    add_text(parts, width - 64, 79, "REV. 1.0", 14, MUTED, font=MONO, anchor="end")
    add_line(parts, 64, 105, width - 64, 105, GRID, 1.2)


def add_section_label(parts, x, y, code, title, color=INK):
    add_text(parts, x, y, code, 14, color, weight=600, font=MONO)
    add_text(parts, x + 50, y, title, 20, color, weight=600)


def add_module(parts, x, y, w, h, title, detail, interface=None):
    add_rect(parts, x, y, w, h, PAPER, LINE, 1.7)
    add_line(parts, x, y, x, y + h, BLUE if interface else LINE, 4)
    add_text(parts, x + 16, y + 30, title, 20, INK, weight=600)
    add_text(parts, x + 16, y + 56, detail, 15, MUTED)
    if interface:
        add_text(parts, x + w - 14, y + 24, interface, 13, BLUE, weight=600, font=MONO, anchor="end")


def add_marker_defs(parts):
    colors = {
        "arrow-line": LINE,
        "arrow-blue": BLUE,
        "arrow-red": RED,
        "arrow-green": GREEN,
        "arrow-amber": AMBER,
    }
    marker_parts = []
    for marker_id, color in colors.items():
        marker_parts.append(
            f'<marker id="{marker_id}" markerWidth="8" markerHeight="8" '
            f'refX="7" refY="4" orient="auto" markerUnits="strokeWidth">'
            f'<path d="M0,0 L8,4 L0,8 z" fill="{color}"/></marker>'
        )
    parts.append("<defs>" + "".join(marker_parts) + "</defs>")


def write_svg(name, width, height, title, body):
    SVG_DIR.mkdir(parents=True, exist_ok=True)
    IMAGE_DIR.mkdir(parents=True, exist_ok=True)
    parts = []
    parts.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}" role="img" '
        f'aria-labelledby="{name}-title {name}-desc">'
    )
    parts.append(f'<title id="{name}-title">{escape(title)}</title>')
    parts.append(
        f'<desc id="{name}-desc">工业采集控制终端的工程框图，'
        f"包含硬件分层、数据路径、总线接口和关键器件。</desc>"
    )
    add_marker_defs(parts)
    parts.append(f'<rect width="{width}" height="{height}" fill="{PAPER}"/>')
    parts.extend(body)
    parts.append("</svg>")
    svg = "".join(parts)

    svg_path = SVG_DIR / f"{name}.svg"
    png_path = SVG_DIR / f"{name}.png"
    webp_path = IMAGE_DIR / f"{name}.webp"
    svg_path.write_text(svg, encoding="utf-8")
    cairosvg.svg2png(
        bytestring=svg.encode("utf-8"),
        write_to=str(png_path),
        output_width=width,
        output_height=height,
    )
    with Image.open(png_path) as image:
        image.convert("RGB").save(webp_path, "WEBP", quality=93, method=6)


def system_architecture():
    width, height = 2000, 1180
    parts = []
    add_title(
        parts,
        width,
        "系统架构 / SYSTEM ARCHITECTURE",
        "STM32F103ZET6 负责硬实时采集与控制，ESP32-S3-N16R8 负责人机界面和网络任务",
    )

    separators = [350, 720, 1450]
    for x in separators:
        add_line(parts, x, 130, x, 965, GRID, 1.2, dash="6 6")
    add_line(parts, 720, 130, 720, 965, RED, 2, dash="10 7")
    add_text(parts, 728, 152, "电气隔离边界", 14, RED)

    add_section_label(parts, 64, 150, "01", "现场侧", AMBER)
    add_section_label(parts, 390, 150, "02", "保护、调理与隔离", RED)
    add_section_label(parts, 770, 150, "03", "控制器侧", BLUE)
    add_section_label(parts, 1490, 150, "04", "应用与网络侧", GREEN)

    field = [
        ("过程量输入", "0-10V / 4-20mA", "AI"),
        ("温度输入", "PT100 / PT1000", "RTD"),
        ("数字输入", "24V 干接点 x8", "DI1..8"),
        ("功率输出", "继电器负载 x8", "DO1..8"),
        ("现场总线", "RS485 / RS232 / CAN", "BUS"),
        ("外部电源", "24V DC / 电池路径", "PWR"),
    ]
    protection = [
        ("模拟前端", "RC 滤波 + ADS1256 + ADR421", "AIN0..7"),
        ("RTD 保护", "MAX31865 + SMBJ5.0CA x3", "PT_RTD"),
        ("数字隔离", "TLP291-4 + 限流与滤波", "GPIO"),
        ("继电器驱动", "ULN2803 + 续流回路", "RELAY"),
        ("隔离收发器", "TD541S485H / S232H / SCANFD", "ISO BUS"),
        ("电源转换", "TPS5430 / AMS1117 / TP5400", "5V / 3.3V"),
    ]
    row_y = [205, 325, 445, 565, 685, 825]
    for index, (title, detail, interface) in enumerate(field):
        add_module(parts, 64, row_y[index], 260, 92, title, detail, interface)
    for index, (title, detail, interface) in enumerate(protection):
        add_module(parts, 390, row_y[index], 294, 92, title, detail, interface)

    add_rect(parts, 770, 205, 640, 365, PAPER, LINE, 2)
    add_line(parts, 770, 205, 1410, 205, BLUE, 5)
    add_text(parts, 798, 246, "STM32F103ZET6", 28, INK, weight=600)
    add_text(parts, 798, 275, "Cortex-M3 / real-time control domain", 15, MUTED, font=MONO)
    add_text(parts, 800, 321, "采集与存储", 16, BLUE, weight=600)
    add_text(parts, 800, 350, "ADS1256  SPI2", 16, MUTED, font=MONO)
    add_text(parts, 800, 376, "MAX31865  SPI3", 16, MUTED, font=MONO)
    add_text(parts, 800, 402, "AT24C32D  I2C2", 16, MUTED, font=MONO)
    add_text(parts, 1060, 321, "本地控制", 16, BLUE, weight=600)
    add_text(parts, 1060, 350, "DI1  PD2", 16, MUTED, font=MONO)
    add_text(parts, 1060, 376, "DI2..8  PG9..PG15", 16, MUTED, font=MONO)
    add_text(parts, 1060, 402, "REL1..8  PD8..PD15", 16, MUTED, font=MONO)
    add_text(parts, 800, 454, "现场通信", 16, BLUE, weight=600)
    add_text(parts, 800, 483, "USART2 RS485   USART4 RS232   CAN1", 16, MUTED, font=MONO)
    add_text(parts, 800, 515, "DAC PA4 / PA5   ADC PC0 / PC1", 16, MUTED, font=MONO)
    add_text(parts, 800, 548, "SWD 下载调试: PA13 / PA14", 16, MUTED)

    add_path(parts, [(1090, 570), (1090, 635)], BLUE, 2.2, arrow="arrow-blue")
    add_text(parts, 1110, 602, "UART1", 17, BLUE, weight=600, font=MONO)
    add_text(parts, 1110, 626, "状态上行 / 命令下行", 14, MUTED)

    add_rect(parts, 770, 650, 640, 292, PAPER, LINE, 2)
    add_line(parts, 770, 650, 1410, 650, GREEN, 5)
    add_text(parts, 798, 691, "ESP32-S3-WROOM-1-N16R8", 28, INK, weight=600)
    add_text(parts, 798, 720, "WiFi / display / audio / remote service", 15, MUTED, font=MONO)
    add_text(parts, 800, 767, "本地界面", 16, GREEN, weight=600)
    add_text(parts, 800, 796, "LCD  SPI: IO41 / IO40 / IO39 / IO10 / IO47", 15, MUTED, font=MONO)
    add_text(parts, 800, 822, "AUDIO  I2C + I2S: IO4 / IO5 / IO6..IO14", 15, MUTED, font=MONO)
    add_text(parts, 800, 848, "PA_EN reserved: IO15", 15, MUTED, font=MONO)
    add_text(parts, 800, 888, "网络与升级", 16, GREEN, weight=600)
    add_text(parts, 800, 917, "WiFi / MQTT / WebSocket / OTA", 15, MUTED, font=MONO)

    applications = [
        ("LCD", "1.54 inch ST7789 / SPI", "J-LCD"),
        ("Audio", "ES8311 + NS4150B / I2C + I2S", "J22"),
        ("Network", "MQTT / WebSocket / OTA", "WIFI"),
        ("Operator", "浏览器 / PC / 手机终端", "TCP/IP"),
    ]
    for index, (title, detail, interface) in enumerate(applications):
        add_module(parts, 1490, 205 + index * 158, 430, 112, title, detail, interface)

    links = [
        (205, "AIN", "arrow-blue"),
        (325, "RTD", "arrow-blue"),
        (445, "DI", "arrow-line"),
        (565, "DO", "arrow-line"),
        (685, "ISO BUS", "arrow-red"),
        (825, "POWER", "arrow-amber"),
    ]
    for index, (y, label, marker) in enumerate(links):
        add_line(parts, 324, y + 46, 390, y + 46, LINE, 1.8, arrow=marker)
        add_text(parts, 330, y + 35, label, 12, MUTED, font=MONO)
    bus_links = [
        (205, 251, "SPI2"),
        (325, 350, "SPI3"),
        (445, 430, "GPIO"),
        (565, 500, "GPIO"),
        (685, 545, "USART / CAN"),
    ]
    for index, (source_y, target_y, label) in enumerate(bus_links):
        color = RED if index == 4 else LINE
        add_path(parts, [(684, source_y + 46), (714, source_y + 46), (714, target_y), (770, target_y)], color, 1.8, arrow="arrow-red" if color == RED else "arrow-line")
        add_text(parts, 654, source_y + 35, label, 12, MUTED, font=MONO, anchor="end")

    add_path(parts, [(684, 871), (746, 871)], LINE, 1.8)
    add_path(parts, [(746, 400), (746, 871)], LINE, 1.8)
    add_path(parts, [(746, 400), (770, 400)], AMBER, 1.8, arrow="arrow-amber")
    add_path(parts, [(746, 800), (770, 800)], AMBER, 1.8, arrow="arrow-amber")
    add_text(parts, 752, 889, "POWER", 12, AMBER, font=MONO)

    add_line(parts, 1410, 783, 1458, 783, LINE, 1.8)
    for index in range(4):
        y = 261 + index * 158
        add_line(parts, 1458, 783, 1458, y, LINE, 1.2)
        add_line(parts, 1458, y, 1490, y, LINE, 1.8, arrow="arrow-green")

    add_text(parts, 64, 1000, "电源路径 / POWER TREE", 16, INK, weight=600)
    power = [
        ("24V DC IN", "防反接 / 浪涌"),
        ("TPS5430", "24V -> 5V"),
        ("AMS1117", "5V -> 3.3V"),
        ("TP5400", "电池充放电"),
        ("模拟电源", "磁珠 / 0R 隔离"),
    ]
    for index, (title, detail) in enumerate(power):
        x = 64 + index * 382
        add_rect(parts, x, 1022, 310, 72, PAPER, LINE, 1.5)
        add_text(parts, x + 16, 1051, title, 18, INK, weight=600)
        add_text(parts, x + 16, 1076, detail, 14, MUTED)
        if index < len(power) - 1:
            add_line(parts, x + 310, 1058, x + 382, 1058, LINE, 1.8, arrow="arrow-amber")

    write_svg("arch-system", width, height, "系统架构", parts)


def data_flow():
    width, height = 2000, 930
    parts = []
    add_title(
        parts,
        width,
        "数据流 / DATA FLOW",
        "上行遥测、下行控制和断网后的本地闭环分别使用独立路径",
    )

    nodes = [
        ("现场信号", "AI / RTD / DI / BUS", 70),
        ("调理与隔离", "滤波 / TVS / 光耦 / 收发器", 450),
        ("STM32", "采样 / 标定 / 报警 / 输出", 830),
        ("ESP32-S3", "LCD / 音频 / WiFi", 1210),
        ("用户与云端", "看板 / MQTT / OTA", 1590),
    ]
    for index, (title, detail, x) in enumerate(nodes):
        add_rect(parts, x, 185, 320, 148, PAPER, LINE, 2)
        add_line(parts, x, 185, x, 333, BLUE, 5)
        add_text(parts, x + 20, 226, title, 25, INK, weight=600)
        add_text(parts, x + 20, 262, detail, 15, MUTED)
        add_text(parts, x + 20, 302, f"NODE {index + 1:02d}", 13, BLUE, font=MONO)

    add_line(parts, 80, 405, 1910, 405, GRID, 1)
    add_text(parts, 70, 389, "UPLINK", 14, BLUE, weight=600, font=MONO)
    for index in range(4):
        add_line(parts, 390 + index * 380, 275, 450 + index * 380, 275, BLUE, 2.4, arrow="arrow-blue")
    add_text(parts, 420, 252, "ADC", 13, BLUE, weight=600, font=MONO, anchor="middle")
    add_text(parts, 800, 252, "LOCAL", 13, BLUE, weight=600, font=MONO, anchor="middle")
    add_text(parts, 1180, 252, "UART", 13, BLUE, weight=600, font=MONO, anchor="middle")
    add_text(parts, 1560, 252, "MQTT", 13, BLUE, weight=600, font=MONO, anchor="middle")

    add_line(parts, 80, 540, 1910, 540, GRID, 1)
    add_text(parts, 70, 524, "DOWNLINK", 14, AMBER, weight=600, font=MONO)
    for index in range(4):
        add_line(parts, 1910 - index * 380, 475, 1750 - index * 380, 475, AMBER, 2.2, arrow="arrow-amber")
    add_text(parts, 1830, 458, "CMD", 13, AMBER, weight=600, font=MONO, anchor="middle")
    add_text(parts, 1450, 458, "UART", 13, AMBER, weight=600, font=MONO, anchor="middle")
    add_text(parts, 1070, 458, "DAC", 13, AMBER, weight=600, font=MONO, anchor="middle")
    add_text(parts, 690, 458, "OUT", 13, AMBER, weight=600, font=MONO, anchor="middle")

    add_line(parts, 80, 695, 1910, 695, GRID, 1)
    add_text(parts, 70, 679, "LOCAL", 14, GREEN, weight=600, font=MONO)
    add_path(parts, [(230, 333), (230, 650), (990, 650), (990, 333)], GREEN, 2.4, arrow="arrow-green")
    add_text(parts, 248, 625, "现场输入 -> STM32 采样、判断、输出", 16, GREEN, weight=600)
    add_text(parts, 1050, 625, "网络中断不影响硬实时路径", 15, MUTED)

    add_rect(parts, 70, 760, 900, 96, PAPER, LINE, 1.5)
    add_text(parts, 92, 799, "板间协议", 18, INK, weight=600)
    add_text(parts, 92, 830, "固定帧头 + 长度 + 序号 + CRC；STM32 上报采集值、状态和报警。", 15, MUTED)
    add_rect(parts, 1030, 760, 900, 96, PAPER, LINE, 1.5)
    add_text(parts, 1052, 799, "显示一致性", 18, INK, weight=600)
    add_text(parts, 1052, 830, "ESP32-S3 使用统一状态快照刷新 LCD、音频提示和网络看板。", 15, MUTED)

    write_svg("arch-data-flow", width, height, "数据流", parts)


def io_topology():
    width, height = 2000, 1280
    parts = []
    add_title(
        parts,
        width,
        "接口拓扑 / INTERFACE TOPOLOGY",
        "总线、器件和关键引脚按实际连接整理，现场总线与板载外设分开",
    )

    add_section_label(parts, 64, 150, "STM32", "实时控制与外设", BLUE)
    add_section_label(parts, 1250, 150, "ESP32-S3", "显示、音频与网络", GREEN)

    stm_x, stm_y, stm_w, stm_h = 720, 190, 470, 900
    add_rect(parts, stm_x, stm_y, stm_w, stm_h, PAPER, LINE, 2.4)
    add_line(parts, stm_x, stm_y, stm_x + stm_w, stm_y, BLUE, 6)
    add_text(parts, stm_x + 26, stm_y + 45, "STM32F103ZET6", 28, INK, weight=600)
    add_text(parts, stm_x + 26, stm_y + 75, "LQFP144 / Cortex-M3", 15, MUTED, font=MONO)

    left_modules = [
        ("ADS1256 + ADR421", "24-bit ADC / 2.5V reference", "SPI2", "PB13..PB15 / PG2..PG5"),
        ("MAX31865ATP+T", "PT100 / PT1000 / TVS x3", "SPI3", "PB3..PB5 / PB8 / PB9"),
        ("AT24C32D", "calibration and configuration", "I2C2", "PB10 / PB11 / 0x50"),
        ("External I2C", "reserved expansion header", "I2C1", "PB6 / PB7"),
        ("Digital inputs", "TLP291-4 x2 / isolated", "GPIO", "PD2 / PG9..PG15"),
        ("Relay output", "ULN2803 / relay x8", "GPIO", "PD8..PD15"),
        ("RS485", "TD541S485H / isolated", "USART2", "PA1 / PA2 / PA3"),
        ("RS232", "TDH541S232H / isolated", "USART4", "PC10 / PC11"),
        ("CAN", "TDH541SCANFD / isolated", "CAN1", "PA11 / PA12"),
    ]
    row_start = 210
    row_step = 92
    for index, (title, detail, bus, pins) in enumerate(left_modules):
        y = row_start + index * row_step
        add_rect(parts, 64, y, 360, 70, PAPER, LINE, 1.5)
        add_text(parts, 80, y + 28, title, 18, INK, weight=600)
        add_text(parts, 80, y + 52, detail, 13, MUTED)
        add_text(parts, 410, y + 56, bus, 14, BLUE, weight=600, font=MONO, anchor="end")
        add_line(parts, 424, y + 35, stm_x, y + 35, LINE, 1.6, arrow="arrow-line")
        add_text(parts, 436, y + 28, pins, 11, MUTED, font=MONO)
        add_text(parts, stm_x + 26, y + 38, bus, 13, BLUE, weight=600, font=MONO)

    esp_x, esp_y, esp_w, esp_h = 1240, 250, 390, 760
    add_rect(parts, esp_x, esp_y, esp_w, esp_h, PAPER, LINE, 2.4)
    add_line(parts, esp_x, esp_y, esp_x + esp_w, esp_y, GREEN, 6)
    add_text(parts, esp_x + 26, esp_y + 45, "ESP32-S3-N16R8", 27, INK, weight=600)
    add_text(parts, esp_x + 26, esp_y + 75, "WiFi / LCD / audio / OTA", 15, MUTED, font=MONO)

    right_modules = [
        ("LCD", "1.54 inch ST7789", "SPI", "IO10 / IO39..IO42 / IO47"),
        ("Audio module", "ES8311 + NS4150B", "I2C + I2S", "IO4..IO6 / IO11..IO14"),
        ("Download", "Type-C + CH340K", "USB + UART0", "IO19 / IO20 / IO43 / IO44"),
        ("Cloud service", "MQTT / WebSocket / OTA", "WiFi", "IO4 / IO5 / radio"),
        ("RGB / spare", "reserved user interface", "GPIO", "IO1 / IO2 / IO48"),
    ]
    right_start = 285
    right_step = 132
    for index, (title, detail, bus, pins) in enumerate(right_modules):
        y = right_start + index * right_step
        add_rect(parts, 1690, y, 246, 88, PAPER, LINE, 1.5)
        add_text(parts, 1706, y + 29, title, 17, INK, weight=600)
        add_text(parts, 1922, y + 29, bus, 12, GREEN, weight=600, font=MONO, anchor="end")
        add_text(parts, 1706, y + 52, detail, 12, MUTED)
        add_text(parts, 1706, y + 76, pins, 10, MUTED, font=MONO)
        add_line(parts, esp_x + esp_w, y + 44, 1690, y + 44, LINE, 1.6)

    add_line(parts, 1190, 360, 1240, 360, GREEN, 2, arrow="arrow-green")
    add_text(parts, 1215, 344, "UART1", 13, GREEN, weight=600, font=MONO, anchor="middle")
    add_line(parts, 1240, 980, 1190, 980, BLUE, 2, arrow="arrow-blue")
    add_text(parts, 1215, 969, "STATUS", 12, BLUE, weight=600, font=MONO, anchor="middle")

    add_rect(parts, 64, 1085, 1872, 118, PAPER, LINE, 1.5)
    add_text(parts, 88, 1124, "总线约定 / BUS RULES", 17, INK, weight=600)
    add_text(parts, 88, 1157, "SPI: high-speed device connection      I2C: low-speed configuration and storage", 15, MUTED, font=MONO)
    add_text(parts, 1050, 1157, "USART: point-to-point or field serial      CAN: multi-node field bus", 15, MUTED, font=MONO)

    add_text(parts, 88, 1230, "保留约束", 15, RED, weight=600)
    add_text(parts, 176, 1230, "PA13 / PA14 保留 SWD；PB3 / PB4 使用 SPI3 后关闭 JTAG；IO35 / IO36 / IO37 被 N16R8 PSRAM 占用；IO45 / IO46 保持启动默认电平。", 14, MUTED)

    write_svg("arch-io-topology", width, height, "接口拓扑", parts)


if __name__ == "__main__":
    system_architecture()
    data_flow()
    io_topology()
