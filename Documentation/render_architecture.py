from html import escape
from pathlib import Path

import cairosvg
from PIL import Image


ROOT = Path(__file__).resolve().parent
IMAGE_DIR = ROOT / "images"
SVG_DIR = ROOT / ".render"

FONT = "Microsoft YaHei, Noto Sans SC, sans-serif"
MONO = "Cascadia Mono, Consolas, monospace"

COLORS = {
    "ink": "#17324D",
    "muted": "#5C7185",
    "line": "#C8D4DF",
    "paper": "#F5F8FB",
    "white": "#FFFFFF",
    "blue": "#2F6FB0",
    "blue_fill": "#E8F1FA",
    "teal": "#278377",
    "teal_fill": "#E6F4F1",
    "amber": "#B87516",
    "amber_fill": "#FBF1DF",
    "red": "#B14E4A",
    "red_fill": "#F8E9E8",
    "violet": "#6E5AA8",
    "violet_fill": "#EFECF8",
    "slate": "#58708A",
    "slate_fill": "#EDF2F6",
}


def add_text(
    parts,
    x,
    y,
    text,
    size=24,
    fill=None,
    weight=400,
    anchor="start",
    font=FONT,
    opacity=1,
):
    fill = fill or COLORS["ink"]
    parts.append(
        f'<text x="{x}" y="{y}" font-family="{font}" font-size="{size}" '
        f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}" '
        f'opacity="{opacity}">{escape(str(text))}</text>'
    )


def add_rect(
    parts,
    x,
    y,
    w,
    h,
    fill,
    stroke="none",
    radius=18,
    stroke_width=1.5,
    opacity=1,
):
    parts.append(
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width}" '
        f'opacity="{opacity}"/>'
    )


def marker_id(color):
    if color == COLORS["amber"]:
        return "arrow-amber"
    if color == COLORS["teal"]:
        return "arrow-teal"
    if color == COLORS["violet"]:
        return "arrow-violet"
    if color == COLORS["red"]:
        return "arrow-red"
    return "arrow-blue"


def add_line(
    parts,
    x1,
    y1,
    x2,
    y2,
    color=None,
    width=2,
    dash=None,
    marker=False,
):
    color = color or COLORS["line"]
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    marker_attr = f' marker-end="url(#{marker_id(color)})"' if marker else ""
    parts.append(
        f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        f'stroke="{color}" stroke-width="{width}"{dash_attr}{marker_attr}/>'
    )


def add_path(parts, d, color, width=3, marker=False, fill="none", dash=None):
    marker_attr = f' marker-end="url(#{marker_id(color)})"' if marker else ""
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    parts.append(
        f'<path d="{d}" fill="{fill}" stroke="{color}" stroke-width="{width}" '
        f'stroke-linecap="round" stroke-linejoin="round"{marker_attr}{dash_attr}/>'
    )


def add_badge(parts, x, y, text, fill, text_fill=None, width=None):
    width = width or max(86, len(text) * 15 + 34)
    add_rect(parts, x, y, width, 40, fill, radius=20)
    add_text(
        parts,
        x + width / 2,
        y + 27,
        text,
        18,
        text_fill or COLORS["ink"],
        weight=500,
        anchor="middle",
    )


def add_card(parts, x, y, w, h, title, lines, color, index=None, title_size=25):
    add_rect(parts, x, y, w, h, COLORS["white"], COLORS["line"], radius=16)
    add_rect(parts, x, y, 8, h, color, radius=4)
    title_y = y + 39
    if index:
        add_text(parts, x + 25, title_y, index, 16, color, weight=500, font=MONO)
        title_x = x + 66
    else:
        title_x = x + 25
    add_text(parts, title_x, title_y, title, title_size, COLORS["ink"], weight=500)
    yy = y + 73
    for line in lines:
        parts.append(f'<circle cx="{x + 31}" cy="{yy - 7}" r="4" fill="{color}"/>')
        add_text(parts, x + 48, yy, line, 19, COLORS["muted"])
        yy += 30


def add_list(parts, x, y, lines, color, line_height=29, font_size=18):
    for line in lines:
        parts.append(f'<circle cx="{x}" cy="{y - 7}" r="4" fill="{color}"/>')
        add_text(parts, x + 18, y, line, font_size, COLORS["muted"])
        y += line_height


def header(parts, width, title, subtitle):
    add_text(parts, 72, 72, title, 40, COLORS["ink"], weight=500)
    add_text(parts, 72, 110, subtitle, 21, COLORS["muted"])
    add_line(parts, 72, 135, width - 72, 135, COLORS["line"], 1.5)


def svg_document(width, height, title, description, body):
    defs = f"""
    <defs>
      <marker id="arrow-blue" markerWidth="11" markerHeight="11" refX="9" refY="5.5"
              orient="auto" markerUnits="strokeWidth">
        <path d="M0,0 L11,5.5 L0,11 z" fill="{COLORS["blue"]}"/>
      </marker>
      <marker id="arrow-amber" markerWidth="11" markerHeight="11" refX="9" refY="5.5"
              orient="auto" markerUnits="strokeWidth">
        <path d="M0,0 L11,5.5 L0,11 z" fill="{COLORS["amber"]}"/>
      </marker>
      <marker id="arrow-teal" markerWidth="11" markerHeight="11" refX="9" refY="5.5"
              orient="auto" markerUnits="strokeWidth">
        <path d="M0,0 L11,5.5 L0,11 z" fill="{COLORS["teal"]}"/>
      </marker>
      <marker id="arrow-violet" markerWidth="11" markerHeight="11" refX="9" refY="5.5"
              orient="auto" markerUnits="strokeWidth">
        <path d="M0,0 L11,5.5 L0,11 z" fill="{COLORS["violet"]}"/>
      </marker>
      <marker id="arrow-red" markerWidth="11" markerHeight="11" refX="9" refY="5.5"
              orient="auto" markerUnits="strokeWidth">
        <path d="M0,0 L11,5.5 L0,11 z" fill="{COLORS["red"]}"/>
      </marker>
      <filter id="shadow" x="-10%" y="-10%" width="120%" height="130%">
        <feDropShadow dx="0" dy="5" stdDeviation="8" flood-color="#17324D"
                      flood-opacity="0.08"/>
      </filter>
    </defs>
    """
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}" role="img" '
        f'aria-labelledby="title desc">'
        f'<title id="title">{escape(title)}</title>'
        f'<desc id="desc">{escape(description)}</desc>'
        f"{defs}"
        f'<rect width="{width}" height="{height}" fill="{COLORS["paper"]}"/>'
        f"{''.join(body)}"
        f"</svg>"
    )


def render(name, width, height, svg):
    SVG_DIR.mkdir(parents=True, exist_ok=True)
    IMAGE_DIR.mkdir(parents=True, exist_ok=True)
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
    width, height = 1800, 1080
    parts = []
    header(
        parts,
        width,
        "工业采集控制终端 · 硬件系统架构",
        "现场信号经保护与隔离进入 STM32；ESP32-S3 负责桥接，并预留联网和远程交互",
    )

    band_y, band_h = 164, 812
    bands = [
        (52, 350, "现场侧", "仪表、执行器和现场总线", COLORS["amber"], COLORS["amber_fill"]),
        (430, 350, "保护与隔离", "调理、驱动和现场保护", COLORS["red"], COLORS["red_fill"]),
        (808, 460, "控制器侧", "实时控制和桥接固件", COLORS["blue"], COLORS["blue_fill"]),
        (1296, 452, "应用与网络侧", "远程服务、调试和扩展接口", COLORS["teal"], COLORS["teal_fill"]),
    ]
    for x, w, title, subtitle, color, fill in bands:
        add_rect(parts, x, band_y, w, band_h, fill, radius=24)
        add_text(parts, x + 28, band_y + 43, title, 26, color, weight=500)
        add_text(parts, x + 28, band_y + 73, subtitle, 17, COLORS["muted"])

    field_cards = [
        ("01", "过程量输入", ["0 到 10V、4 到 20mA"], 118),
        ("02", "温度与触点", ["PT100 / PT1000", "24V 干接点 x8"], 150),
        ("03", "现场总线", ["RS485 / RS232 / CAN"], 118),
        ("04", "执行器与电源", ["继电器负载 x8", "24V DC 输入"], 150),
    ]
    y = 276
    for index, title, lines, card_h in field_cards:
        add_card(parts, 74, y, 304, card_h, title, lines, COLORS["amber"], index)
        y += card_h + 20

    protection_cards = [
        ("05", "模拟前端", ["RC 滤波 + ADS1256", "ADR421 2.5V 基准"], 150),
        ("06", "RTD 保护", ["MAX31865 + 三路 TVS", "100nF 差分滤波"], 150),
        ("07", "数字隔离与驱动", ["TLP291-4 光耦输入", "ULN2803 继电器驱动"], 150),
        ("08", "输出与隔离通信", ["LM358 电压和电流输出", "TD541 系列收发器"], 150),
    ]
    y = 276
    for index, title, lines, card_h in protection_cards:
        add_card(parts, 452, y, 306, card_h, title, lines, COLORS["red"], index)
        y += card_h + 20

    ctrl_x = 834
    add_rect(parts, ctrl_x, 276, 408, 312, COLORS["white"], COLORS["line"], 18)
    add_badge(parts, ctrl_x + 24, 296, "实时控制域", COLORS["blue_fill"], COLORS["blue"], 170)
    add_text(parts, ctrl_x + 24, 385, "STM32F103ZET6", 31, COLORS["ink"], weight=500)
    add_text(parts, ctrl_x + 24, 418, "Cortex-M3 · FreeRTOS", 19, COLORS["muted"])
    add_list(
        parts,
        ctrl_x + 34,
        462,
        [
            "ADS1256 · SPI2 · 八通道 24 位",
            "MAX31865 · SPI3 · PT100 / PT1000",
            "AT24C32D · I2C2 · 参数与校准",
            "继电器 / 光耦 / DAC / 电源监测",
        ],
        COLORS["blue"],
        29,
        18,
    )

    add_rect(parts, ctrl_x, 622, 408, 74, COLORS["violet_fill"], radius=18)
    add_text(parts, ctrl_x + 28, 654, "UART1 桥接", 22, COLORS["violet"], weight=500)
    add_text(parts, ctrl_x + 28, 681, "PA9 / PA10 · 115200 8N1", 18, COLORS["muted"])
    add_line(parts, ctrl_x + 204, 588, ctrl_x + 204, 622, COLORS["violet"], 4, marker=True)
    add_line(parts, ctrl_x + 204, 696, ctrl_x + 204, 730, COLORS["violet"], 4, marker=True)

    add_rect(parts, ctrl_x, 730, 408, 218, COLORS["white"], COLORS["line"], 18)
    add_badge(parts, ctrl_x + 24, 750, "桥接与网络域", COLORS["teal_fill"], COLORS["teal"], 170)
    add_text(parts, ctrl_x + 24, 837, "ESP32-S3-N16R8", 30, COLORS["ink"], weight=500)
    add_list(
        parts,
        ctrl_x + 34,
        878,
        [
            "UART1 · IO17 / IO18",
            "uart_rx / heartbeat / command_router",
            "WiFi、MQTT 和 OTA 保留为扩展点",
        ],
        COLORS["teal"],
        29,
        18,
    )

    app_x = 1318
    add_card(
        parts,
        app_x,
        276,
        408,
        160,
        "远程应用",
        ["WiFi / MQTT / WebSocket", "OTA 与远程配置扩展点"],
        COLORS["violet"],
        "09",
    )
    add_card(
        parts,
        app_x,
        458,
        408,
        160,
        "可选本地界面",
        ["LCD 与音频接口已预留", "当前固件不初始化这两项"],
        COLORS["slate"],
        "10",
    )
    add_card(
        parts,
        app_x,
        640,
        408,
        160,
        "下载与调试",
        ["USB Type-C 与 UART0", "BOOT、复位和 SWD"],
        COLORS["teal"],
        "11",
    )
    add_card(
        parts,
        app_x,
        822,
        408,
        126,
        "使用者与系统",
        ["PC、手机、云端看板"],
        COLORS["blue"],
        "12",
    )

    add_line(parts, 402, 445, 430, 445, COLORS["amber"], 5, marker=True)
    add_line(parts, 780, 445, 808, 445, COLORS["red"], 5, marker=True)
    add_line(parts, 1268, 445, 1296, 445, COLORS["teal"], 5, marker=True)

    add_rect(parts, 52, 1004, 1696, 52, COLORS["white"], COLORS["line"], 14)
    add_text(
        parts,
        74,
        1037,
        "设计原则：STM32 保持实时采集和控制；ESP32-S3 隔离网络任务；现场接口独立供电并经过保护、隔离和滤波。",
        19,
        COLORS["muted"],
    )

    render(
        "arch-system",
        width,
        height,
        svg_document(
            width,
            height,
            "硬件系统架构",
            "工业采集控制终端的硬件分层、核心器件和处理器分工。",
            parts,
        ),
    )


def data_flow():
    width, height = 1800, 900
    parts = []
    header(
        parts,
        width,
        "采集、控制与远程交互 · 数据流",
        "蓝色表示上行状态，橙色表示下行控制，绿色表示不依赖网络的本地闭环",
    )

    stages = [
        ("01", "现场信号", ["AI / RTD / DI", "RS485 / RS232 / CAN"], COLORS["amber"]),
        ("02", "调理与隔离", ["滤波、TVS、光耦", "隔离电源与驱动"], COLORS["red"]),
        ("03", "STM32", ["采样、标定、报警", "继电器与 DAC 输出"], COLORS["blue"]),
        ("04", "ESP32-S3", ["状态缓存与命令路由", "UART1 桥接"], COLORS["teal"]),
        ("05", "网络与远程", ["WiFi / MQTT / OTA", "当前为扩展点"], COLORS["violet"]),
    ]
    card_w, gap = 310, 34
    for i, (index, title, lines, color) in enumerate(stages):
        x = 62 + i * (card_w + gap)
        y = 218
        add_rect(parts, x, y, card_w, 246, COLORS["white"], COLORS["line"], 18)
        add_rect(parts, x, y, card_w, 8, color, radius=4)
        add_text(parts, x + 24, y + 50, index, 17, color, weight=500, font=MONO)
        add_text(parts, x + 70, y + 52, title, 28, COLORS["ink"], weight=500)
        for j, line in enumerate(lines):
            yy = y + 112 + j * 44
            add_rect(parts, x + 24, yy - 25, card_w - 48, 36, COLORS["slate_fill"], radius=10)
            add_text(parts, x + 40, yy, line, 18, COLORS["muted"])

    for i in range(4):
        x = 62 + i * (card_w + gap)
        add_line(parts, x + card_w + 6, 340, x + card_w + gap - 6, 340, COLORS["blue"], 5, marker=True)

    add_badge(parts, 430, 174, "上行：采样、状态、心跳与事件", COLORS["blue_fill"], COLORS["blue"], 382)
    add_badge(parts, 986, 520, "下行：命令、配置与确认", COLORS["amber_fill"], COLORS["amber"], 346)
    for i in (4, 3, 2):
        x = 62 + i * (card_w + gap)
        add_line(parts, x - 6, 582, x - gap + 6, 582, COLORS["amber"], 5, marker=True)

    add_rect(parts, 168, 660, 672, 116, COLORS["teal_fill"], radius=18)
    add_text(parts, 198, 702, "本地闭环", 25, COLORS["teal"], weight=500)
    add_text(parts, 198, 739, "网络或 ESP32-S3 异常时，STM32 继续采样、报警并执行安全策略。", 19, COLORS["muted"])
    add_path(parts, "M 840 718 C 934 718, 964 625, 964 484", COLORS["teal"], 5, marker=True)

    add_rect(parts, 890, 660, 348, 116, COLORS["white"], COLORS["line"], 18)
    add_text(parts, 918, 702, "状态一致性", 25, COLORS["ink"], weight=500)
    add_text(parts, 918, 739, "device_state 使用 mutex 提供一致快照。", 18, COLORS["muted"])

    add_rect(parts, 1288, 660, 450, 116, COLORS["white"], COLORS["line"], 18)
    add_text(parts, 1316, 702, "板间协议", 25, COLORS["ink"], weight=500)
    add_text(parts, 1316, 739, "固定帧头、长度、序号和 CRC-16 校验。", 18, COLORS["muted"])

    render(
        "arch-data-flow",
        width,
        height,
        svg_document(
            width,
            height,
            "硬件数据流",
            "现场采集、本地控制、板间通信和远程交互的数据路径。",
            parts,
        ),
    )


def io_topology():
    width, height = 1800, 1160
    parts = []
    header(
        parts,
        width,
        "控制器与外设 · 接口拓扑",
        "总线、器件和引脚按实际连接整理，板间 UART 是 STM32 和 ESP32-S3 的唯一控制通道",
    )

    add_rect(parts, 74, 184, 720, 802, COLORS["blue_fill"], radius=24)
    add_text(parts, 104, 232, "STM32F103ZET6", 34, COLORS["blue"], weight=500)
    add_text(parts, 104, 267, "实时采集、控制、存储和现场通信", 19, COLORS["muted"])

    stm_groups = [
        ("高精度采集", "SPI2", ["ADS1256 八通道 ADC", "ADR421 2.5V 基准", "CS / DRDY / RESET / SYNC"], COLORS["red"]),
        ("温度采集", "SPI3", ["MAX31865ATP+T", "三路 SMBJ5.0CA TVS", "100nF 差分滤波"], COLORS["amber"]),
        ("存储与扩展", "I2C2 / I2C1", ["AT24C32D 地址 0x50", "外部 I2C 扩展接口", "4.7k 上拉与总线隔离"], COLORS["violet"]),
        ("数字量输入", "GPIO", ["TLP291-4 八路隔离", "PD2 / PG9 到 PG15", "现场侧独立地"], COLORS["amber"]),
        ("输出与监测", "GPIO / DAC / ADC", ["ULN2803 + 八路继电器", "0 到 10V / 4 到 20mA", "24V 与 5V 电源监测"], COLORS["blue"]),
        ("现场通信", "USART / CAN", ["USART2  RS485", "USART4  RS232", "CAN1  TDH541SCANFD"], COLORS["teal"]),
    ]
    for i, (title, bus, lines, color) in enumerate(stm_groups):
        col, row = i % 2, i // 2
        x = 104 + col * 320
        y = 320 + row * 212
        add_rect(parts, x, y, 286, 178, COLORS["white"], COLORS["line"], 16)
        add_text(parts, x + 20, y + 35, title, 23, COLORS["ink"], weight=500)
        add_badge(parts, x + 20, y + 50, bus, COLORS["slate_fill"], color, min(244, max(82, len(bus) * 14 + 34)))
        add_list(parts, x + 34, y + 122, lines, color, 23, 16)

    add_rect(parts, 1006, 184, 720, 802, COLORS["teal_fill"], radius=24)
    add_text(parts, 1036, 232, "ESP32-S3-N16R8", 34, COLORS["teal"], weight=500)
    add_text(parts, 1036, 267, "协议桥接、状态缓存和远程服务入口", 19, COLORS["muted"])

    esp_groups = [
        ("板间桥接", "UART1", ["IO17 TX / IO18 RX", "115200 8N1", "HELLO、心跳、遥测和命令"], COLORS["teal"]),
        ("下载与调试", "USB / UART0", ["IO19 / IO20 USB", "IO43 / IO44 串口", "BOOT 与复位控制"], COLORS["slate"]),
        ("网络扩展", "WiFi", ["2.4GHz 网络接入", "MQTT / WebSocket / OTA", "当前固件未启动网络任务"], COLORS["violet"]),
        ("可选本地外设", "LCD / I2C / I2S", ["LCD SPI 接口预留", "ES8311 + NS4150B 接口预留", "核心固件不初始化"], COLORS["amber"]),
    ]
    for i, (title, bus, lines, color) in enumerate(esp_groups):
        col, row = i % 2, i // 2
        x = 1036 + col * 320
        y = 320 + row * 212
        add_rect(parts, x, y, 286, 178, COLORS["white"], COLORS["line"], 16)
        add_text(parts, x + 20, y + 35, title, 23, COLORS["ink"], weight=500)
        add_badge(parts, x + 20, y + 50, bus, COLORS["slate_fill"], color, min(244, max(82, len(bus) * 14 + 34)))
        add_list(parts, x + 34, y + 122, lines, color, 23, 16)

    add_rect(parts, 794, 430, 212, 300, COLORS["white"], COLORS["line"], 18)
    add_text(parts, 900, 478, "板间通信", 27, COLORS["violet"], weight=500, anchor="middle")
    add_badge(parts, 820, 500, "UART1", COLORS["violet_fill"], COLORS["violet"], 160)
    add_text(parts, 900, 584, "STM32 PA9", 18, COLORS["muted"], anchor="middle")
    add_text(parts, 900, 616, "ESP32 IO18", 18, COLORS["muted"], anchor="middle")
    add_text(parts, 900, 658, "TX / RX 交叉", 18, COLORS["blue"], anchor="middle")
    add_text(parts, 900, 690, "双方共地", 18, COLORS["blue"], anchor="middle")
    add_path(parts, "M 794 520 C 760 520, 760 500, 760 500", COLORS["blue"], 4, marker=True)
    add_path(parts, "M 1006 642 C 1040 642, 1040 662, 1040 662", COLORS["amber"], 4, marker=True)

    add_rect(parts, 74, 1018, 1652, 96, COLORS["white"], COLORS["line"], 16)
    add_text(parts, 98, 1055, "接口原则", 19, COLORS["ink"], weight=500)
    add_text(parts, 196, 1055, "SPI 连接高速器件，I2C 连接低速配置，UART 负责板间和现场串口，CAN 负责多节点现场总线。", 18, COLORS["muted"])
    add_text(parts, 98, 1090, "保留约束", 19, COLORS["red"], weight=500)
    add_text(parts, 196, 1090, "PA13 / PA14 保留 SWD；PB3 / PB4 使用 SPI3 后关闭 JTAG；IO35 到 IO37 被 N16R8 PSRAM 占用。", 18, COLORS["muted"])

    render(
        "arch-io-topology",
        width,
        height,
        svg_document(
            width,
            height,
            "硬件接口拓扑",
            "STM32、ESP32-S3、现场总线和可选外设接口的拓扑关系。",
            parts,
        ),
    )


def software_architecture():
    width, height = 1800, 1120
    parts = []
    header(
        parts,
        width,
        "工业采集控制终端 · 软件架构",
        "STM32 负责实时闭环，共享协议保证两端一致，ESP32-S3 负责桥接和后续网络服务",
    )

    add_rect(parts, 62, 176, 690, 750, COLORS["blue_fill"], radius=24)
    add_text(parts, 92, 222, "STM32 实时固件", 32, COLORS["blue"], weight=500)
    add_text(parts, 92, 255, "FreeRTOS + CMSIS-RTOS V2", 18, COLORS["muted"])

    stm_tasks = [
        ("01", "monitorTask", "50ms 采样电源，检查任务活性并刷新 IWDG"),
        ("02", "controlTask", "5ms 数字输入消抖、继电器同步和事件队列"),
        ("03", "acqTask", "100ms 读取 ADS1256 八通道原始值"),
        ("04", "rtdTask", "500ms 读取 MAX31865 温度"),
        ("05", "bridgeTask", "解析命令、发送心跳、遥测和事件，缓存请求结果"),
        ("06", "modbusTask", "2ms 轮询 RS485，处理 0x03、0x06 和 0x10"),
    ]
    y = 270
    for index, title, detail in stm_tasks:
        add_rect(parts, 90, y, 634, 72, COLORS["white"], COLORS["line"], 14)
        add_rect(parts, 90, y, 7, 72, COLORS["blue"], radius=3)
        add_text(parts, 116, y + 29, index, 15, COLORS["blue"], weight=500, font=MONO)
        add_text(parts, 164, y + 30, title, 22, COLORS["ink"], weight=500, font=MONO)
        add_text(parts, 164, y + 57, detail, 17, COLORS["muted"])
        y += 76

    add_rect(parts, 90, 760, 634, 132, COLORS["white"], COLORS["line"], 16)
    add_text(parts, 116, 797, "服务与驱动", 23, COLORS["ink"], weight=500)
    add_text(parts, 116, 829, "device_state · ADS1256 · MAX31865 · digital_input · relay_output", 16, COLORS["muted"])
    add_text(parts, 116, 859, "analog_output · config_store · field_comm · diagnostics", 16, COLORS["muted"])
    add_text(parts, 116, 885, "device_state 使用 mutex 提供统一状态快照。", 16, COLORS["blue"])

    add_rect(parts, 786, 176, 326, 750, COLORS["violet_fill"], radius=24)
    add_text(parts, 816, 222, "共享协议", 32, COLORS["violet"], weight=500)
    add_text(parts, 816, 255, "bridge_protocol", 18, COLORS["muted"], font=MONO)
    add_rect(parts, 812, 285, 274, 210, COLORS["white"], COLORS["line"], 16)
    add_text(parts, 838, 323, "帧编解码", 23, COLORS["ink"], weight=500)
    add_list(parts, 848, 366, ["AA 55 帧头", "version / type", "sequence / length", "payload / CRC-16"], COLORS["violet"], 29, 18)
    add_rect(parts, 812, 518, 274, 188, COLORS["white"], COLORS["line"], 16)
    add_text(parts, 838, 556, "消息集合", 23, COLORS["ink"], weight=500)
    add_list(parts, 848, 586, ["HELLO / HEARTBEAT", "TELEMETRY / EVENT", "DIAGNOSTICS", "COMMAND / ACK"], COLORS["violet"], 24, 16)
    add_rect(parts, 812, 728, 274, 164, COLORS["white"], COLORS["line"], 16)
    add_text(parts, 838, 766, "代码复用", 23, COLORS["ink"], weight=500)
    add_list(parts, 848, 809, ["只依赖 C 标准库", "STM32 与 ESP32 同源", "字段布局一致"], COLORS["violet"], 28, 17)

    add_rect(parts, 1146, 176, 592, 750, COLORS["teal_fill"], radius=24)
    add_text(parts, 1176, 222, "ESP32-S3 桥接固件", 32, COLORS["teal"], weight=500)
    add_text(parts, 1176, 255, "ESP-IDF 6.1 + FreeRTOS + WiFi/MQTT", 18, COLORS["muted"])

    esp_tasks = [
        ("07", "uart_rx", "优先级 8，读取 UART1 并逐字节解析"),
        ("08", "heartbeat", "优先级 6，发送 HELLO 和心跳，判断 3 秒离线"),
        ("09", "command_router", "优先级 6，UART0 控制台、ACK 匹配和超时重试"),
        ("10", "network", "优先级 5，WiFi 连接、MQTT JSON 遥测和 NVS 配置"),
    ]
    y = 270
    for index, title, detail in esp_tasks:
        add_rect(parts, 1174, y, 536, 72, COLORS["white"], COLORS["line"], 14)
        add_rect(parts, 1174, y, 7, 72, COLORS["teal"], radius=3)
        add_text(parts, 1200, y + 29, index, 15, COLORS["teal"], weight=500, font=MONO)
        add_text(parts, 1248, y + 30, title, 22, COLORS["ink"], weight=500, font=MONO)
        add_text(parts, 1200, y + 57, detail, 17, COLORS["muted"])
        y += 76

    add_rect(parts, 1174, 618, 536, 122, COLORS["white"], COLORS["line"], 16)
    add_text(parts, 1200, 655, "状态与命令数据", 23, COLORS["ink"], weight=500)
    add_text(parts, 1200, 688, "s_telemetry 保存最近一次完整遥测。", 17, COLORS["muted"])
    add_text(parts, 1200, 718, "command / ack queue 保存命令并等待确认。", 17, COLORS["muted"])

    add_rect(parts, 1174, 762, 536, 130, COLORS["white"], COLORS["line"], 16)
    add_text(parts, 1200, 799, "后续应用扩展点", 23, COLORS["violet"], weight=500)
    add_text(parts, 1200, 832, "WebSocket / OTA / LCD / 音频", 18, COLORS["muted"])
    add_text(parts, 1200, 866, "WiFi 与 MQTT 已实现，其余保留为扩展点。", 17, COLORS["muted"])

    add_line(parts, 752, 500, 786, 500, COLORS["violet"], 5, marker=True)
    add_line(parts, 1112, 500, 1146, 500, COLORS["violet"], 5, marker=True)

    add_rect(parts, 62, 952, 1676, 98, COLORS["white"], COLORS["line"], 16)
    add_text(parts, 92, 990, "运行平台", 23, COLORS["ink"], weight=500)
    add_text(parts, 92, 1024, "STM32：HAL + FreeRTOS + CMSIS-RTOS V2", 18, COLORS["muted"])
    add_text(parts, 696, 1024, "共享层：C 标准库协议代码", 18, COLORS["muted"])
    add_text(parts, 1110, 1024, "ESP32-S3：ESP-IDF 6.1 + FreeRTOS + NVS", 18, COLORS["muted"])

    render(
        "sw-architecture",
        width,
        height,
        svg_document(
            width,
            height,
            "软件系统架构",
            "STM32 任务、共享协议模块、ESP32-S3 桥接任务和运行平台。",
            parts,
        ),
    )


def software_task_flow():
    width, height = 1800, 1060
    parts = []
    header(
        parts,
        width,
        "FreeRTOS 调度与任务通信",
        "1 ms 系统节拍、抢占式调度、任务间队列、互斥锁和软件定时器共同组成实时运行框架",
    )

    add_rect(parts, 62, 178, 460, 388, COLORS["blue_fill"], radius=22)
    add_text(parts, 92, 224, "FreeRTOS 调度内核", 30, COLORS["blue"], weight=500)
    add_badge(parts, 92, 250, "抢占式调度", COLORS["white"], COLORS["blue"], 148)
    add_badge(parts, 252, 250, "1 ms tick", COLORS["white"], COLORS["blue"], 122)
    add_list(
        parts,
        112,
        324,
        [
            "configUSE_PREEMPTION = 1",
            "configTICK_RATE_HZ = 1000",
            "configUSE_MUTEXES = 1",
            "configUSE_TIMERS = 1",
            "6 个任务活性监督",
            "IWDG 4s 看门狗刷新",
            "heap_4 动态内存池 12KB",
            "CMSIS-RTOS V2 统一 API",
        ],
        COLORS["blue"],
        29,
        17,
    )

    add_rect(parts, 550, 178, 700, 388, COLORS["violet_fill"], radius=22)
    add_text(parts, 580, 224, "任务优先级与执行周期", 30, COLORS["violet"], weight=500)
    priority_rows = [
        ("High", "controlTask   5 ms", "acqTask   100 ms", COLORS["red"], COLORS["red_fill"]),
        ("AboveNormal", "monitorTask   50 ms", "rtdTask   500 ms", COLORS["amber"], COLORS["amber_fill"]),
        ("Normal", "bridgeTask   10 ms loop", "modbusTask   2 ms poll", COLORS["blue"], COLORS["blue_fill"]),
        ("Timer", "8 个一次性继电器定时器", "到期自动撤销输出位", COLORS["teal"], COLORS["teal_fill"]),
    ]
    y = 272
    for priority, left, right, color, fill in priority_rows:
        add_rect(parts, 580, y, 640, 62, COLORS["white"], COLORS["line"], 13)
        add_badge(parts, 594, y + 11, priority, fill, color, 132)
        add_text(parts, 752, y + 27, left, 17, COLORS["ink"], weight=500, font=MONO)
        add_text(parts, 752, y + 51, right, 16, COLORS["muted"])
        y += 72

    add_rect(parts, 1278, 178, 460, 388, COLORS["teal_fill"], radius=22)
    add_text(parts, 1308, 224, "任务间通信与同步", 30, COLORS["teal"], weight=500)
    ipc_items = [
        ("s_uart_rx_queue", "512 字节接收队列"),
        ("s_event_queue", "16 条事件消息"),
        ("deviceStateMutex", "保护统一状态快照"),
        ("osTimerOnce x8", "继电器脉冲超时"),
        ("command / ack queue", "ESP32 命令与确认队列"),
        ("portMUX", "ESP32 在线状态临界区"),
    ]
    y = 264
    for title, detail in ipc_items:
        add_rect(parts, 1308, y, 400, 44, COLORS["white"], COLORS["line"], 12)
        add_text(parts, 1328, y + 22, title, 16, COLORS["teal"], weight=500, font=MONO)
        add_text(parts, 1328, y + 39, detail, 14, COLORS["muted"])
        y += 50

    add_rect(parts, 62, 610, 1676, 390, COLORS["white"], COLORS["line"], 18)
    add_text(parts, 92, 655, "运行时路径", 28, COLORS["ink"], weight=500)
    path_cards = [
        ("中断与 DMA", COLORS["amber"], ["ADC1 DMA", "UART 空闲 DMA", "RxEventCallback", "只投递数据"]),
        ("采集与控制任务", COLORS["blue"], ["controlTask 5ms", "acqTask 100ms", "rtdTask 500ms", "monitorTask 50ms", "modbusTask 2ms"]),
        ("状态快照", COLORS["violet"], ["device_state", "mutex 加锁", "DeviceState_Get()", "一致副本"]),
        ("协议桥接", COLORS["teal"], ["bridgeTask", "HEARTBEAT 1s", "TELEMETRY 100ms", "EVENT 队列"]),
        ("ESP32-S3", COLORS["teal"], ["console command", "command_router + ACK", "uart_rx  priority 8", "heartbeat  priority 6", "network + MQTT"]),
    ]
    x = 94
    for title, color, lines in path_cards:
        add_rect(parts, x, 690, 300, 205, COLORS["paper"], COLORS["line"], 15)
        add_rect(parts, x, 690, 300, 7, color, radius=3)
        add_text(parts, x + 20, 730, title, 22, COLORS["ink"], weight=500)
        add_list(parts, x + 32, 770, lines, color, 29, 16)
        if x < 1394:
            add_line(parts, x + 306, 785, x + 350, 785, COLORS["blue"], 5, marker=True)
        x += 350

    add_rect(parts, 94, 910, 1594, 62, COLORS["blue_fill"], radius=14)
    add_text(
        parts,
        120,
        948,
        "中断只搬运数据；任务监督通过后才刷新 IWDG；bridge 和 Modbus 使用独立接收路径。",
        19,
        COLORS["blue"],
    )

    render(
        "sw-task-flow",
        width,
        height,
        svg_document(
            width,
            height,
            "FreeRTOS 调度与任务通信",
            "STM32 与 ESP32-S3 的 FreeRTOS 调度、任务优先级、队列、互斥锁和软件定时器。",
            parts,
        ),
    )


def software_protocol_flow():
    width, height = 1800, 1080
    parts = []
    header(
        parts,
        width,
        "板间通信 · 协议与命令流程",
        "UART1 使用固定二进制帧；遥测可以跳过，心跳、事件和命令确认保持独立",
    )

    add_rect(parts, 90, 188, 520, 142, COLORS["blue_fill"], radius=20)
    add_text(parts, 120, 235, "STM32 实时端", 30, COLORS["blue"], weight=500)
    add_text(parts, 120, 273, "bridgeTask · PA9 / PA10", 19, COLORS["muted"])
    add_text(parts, 120, 306, "采样、控制、事件和命令执行", 18, COLORS["muted"])

    add_rect(parts, 1190, 188, 520, 142, COLORS["teal_fill"], radius=20)
    add_text(parts, 1220, 235, "ESP32-S3 桥接端", 30, COLORS["teal"], weight=500)
    add_text(parts, 1220, 273, "uart_rx · heartbeat · IO17 / IO18", 19, COLORS["muted"])
    add_text(parts, 1220, 306, "接收状态、维护在线判断和命令入口", 18, COLORS["muted"])

    add_line(parts, 610, 230, 1190, 230, COLORS["blue"], 5, marker=True)
    add_line(parts, 1190, 286, 610, 286, COLORS["violet"], 5, marker=True)
    add_text(parts, 900, 218, "HELLO / HEARTBEAT / TELEMETRY / EVENT / DIAG", 19, COLORS["blue"], weight=500, anchor="middle")
    add_text(parts, 900, 316, "HELLO / HEARTBEAT / COMMAND", 20, COLORS["violet"], weight=500, anchor="middle")

    add_text(parts, 90, 392, "固定帧结构", 26, COLORS["ink"], weight=500)
    fields = [
        ("AA 55", "帧头", 112, COLORS["violet"]),
        ("version", "版本", 128, COLORS["blue"]),
        ("type", "类型", 112, COLORS["blue"]),
        ("sequence", "序号", 142, COLORS["teal"]),
        ("length", "载荷长度", 142, COLORS["teal"]),
        ("payload", "0 到 256 字节", 220, COLORS["amber"]),
        ("CRC-16", "校验", 150, COLORS["red"]),
    ]
    x = 90
    for value, label, box_w, color in fields:
        add_rect(parts, x, 420, box_w, 94, COLORS["white"], COLORS["line"], 14)
        add_rect(parts, x, 420, box_w, 7, color, radius=3)
        add_text(parts, x + box_w / 2, 464, value, 20, color, weight=500, font=MONO, anchor="middle")
        add_text(parts, x + box_w / 2, 493, label, 16, COLORS["muted"], anchor="middle")
        x += box_w + 18

    add_rect(parts, 90, 548, 806, 340, COLORS["white"], COLORS["line"], 18)
    add_text(parts, 120, 591, "接收状态机", 27, COLORS["ink"], weight=500)
    state_steps = [
        ("01", "搜索帧头 AA 55"),
        ("02", "读取固定头部"),
        ("03", "检查 version 和 length"),
        ("04", "收齐 payload 和 CRC"),
        ("05", "校验 CRC-16"),
        ("06", "按 type 分发消息"),
    ]
    y = 640
    for index, step in state_steps:
        add_badge(parts, 126, y - 26, index, COLORS["slate_fill"], COLORS["slate"], 58)
        add_text(parts, 210, y, step, 20, COLORS["muted"])
        if index != "06":
            add_line(parts, 155, y + 8, 155, y + 34, COLORS["slate"], 2)
        y += 38
    add_text(parts, 126, 874, "校验失败、版本不支持或接收超时时，丢弃当前帧并从 AA 55 重新同步。", 17, COLORS["red"])

    add_rect(parts, 930, 548, 780, 340, COLORS["white"], COLORS["line"], 18)
    add_text(parts, 960, 591, "命令与确认", 27, COLORS["ink"], weight=500)
    command_steps = [
        ("01", "command_router 建立请求"),
        ("02", "发送 COMMAND 并等待 ACK"),
        ("03", "超时后用同一 request_id 重试"),
        ("04", "STM32 校验参数并查询结果缓存"),
        ("05", "执行继电器、DAC 或配置操作"),
        ("06", "返回 ACK 并由 ESP32 更新状态"),
    ]
    y = 640
    for index, step in command_steps:
        add_badge(parts, 966, y - 26, index, COLORS["amber_fill"], COLORS["amber"], 58)
        add_text(parts, 1050, y, step, 20, COLORS["muted"])
        if index != "06":
            add_line(parts, 995, y + 8, 995, y + 34, COLORS["amber"], 2)
        y += 38
    add_text(parts, 966, 874, "命令重试复用 request_id，STM32 缓存最近 16 条结果并去重。", 17, COLORS["amber"])

    add_rect(parts, 90, 924, 1620, 92, COLORS["violet_fill"], radius=16)
    add_text(parts, 120, 962, "协议约定", 22, COLORS["violet"], weight=500)
    add_text(parts, 120, 997, "所有多字节字段使用小端序；CRC 覆盖 version 到 payload；双方每秒发送心跳，连续 3 秒无有效帧时标记链路离线。", 18, COLORS["muted"])

    render(
        "sw-protocol-flow",
        width,
        height,
        svg_document(
            width,
            height,
            "板间协议与命令流程",
            "STM32 与 ESP32-S3 之间的帧结构、接收状态机和命令确认流程。",
            parts,
        ),
    )


if __name__ == "__main__":
    system_architecture()
    data_flow()
    io_topology()
    software_architecture()
    software_task_flow()
    software_protocol_flow()
